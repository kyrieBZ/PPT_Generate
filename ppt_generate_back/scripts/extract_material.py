#!/usr/bin/env python3
"""
extract_material.py  —  文档关键信息提取脚本

用法:
  python3 extract_material.py --file <路径> --type <pdf|docx|txt> --api-key <qwen_key>

输出: 标准输出 JSON
  {
    "title": "...",
    "summary": "...",
    "outline": [...],
    "key_points": [...],
    "data_mentions": [...],
    "keywords": [...]
  }
"""

import argparse
import json
import sys
import textwrap
import re
import urllib.request
import urllib.error


# ---------------------------------------------------------------------------
# 文本提取
# ---------------------------------------------------------------------------

def extract_text_from_pdf(file_path: str) -> str:
    try:
        import pdfplumber
        text_parts = []
        with pdfplumber.open(file_path) as pdf:
            for page in pdf.pages:
                page_text = page.extract_text()
                if page_text:
                    text_parts.append(page_text)
        return "\n".join(text_parts)
    except ImportError:
        # 回退：尝试 PyMuPDF
        try:
            import fitz  # PyMuPDF
            doc = fitz.open(file_path)
            text_parts = []
            for page in doc:
                text_parts.append(page.get_text())
            return "\n".join(text_parts)
        except ImportError:
            raise RuntimeError(
                "缺少 PDF 解析库，请安装: pip3 install pdfplumber 或 pip3 install PyMuPDF"
            )


def extract_text_from_docx(file_path: str) -> str:
    try:
        from docx import Document
        doc = Document(file_path)
        paragraphs = [p.text for p in doc.paragraphs if p.text.strip()]
        return "\n".join(paragraphs)
    except ImportError:
        raise RuntimeError(
            "缺少 DOCX 解析库，请安装: pip3 install python-docx"
        )


def extract_text_from_txt(file_path: str) -> str:
    for encoding in ("utf-8", "gbk", "latin-1"):
        try:
            with open(file_path, "r", encoding=encoding) as f:
                return f.read()
        except UnicodeDecodeError:
            continue
    raise RuntimeError("无法解码 TXT 文件，请确认文件编码")


def extract_raw_text(file_path: str, file_type: str) -> str:
    file_type = file_type.lower().lstrip(".")
    if file_type == "pdf":
        return extract_text_from_pdf(file_path)
    elif file_type in ("docx", "doc"):
        return extract_text_from_docx(file_path)
    elif file_type == "txt":
        return extract_text_from_txt(file_path)
    else:
        raise ValueError(f"不支持的文件类型: {file_type}")


# ---------------------------------------------------------------------------
# Qwen API 调用
# ---------------------------------------------------------------------------

QWEN_ENDPOINT = "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation"
MAX_TEXT_CHARS = 12000  # 截断过长文本，避免超出 token 限制


def build_extract_prompt(raw_text: str) -> str:
    truncated = raw_text[:MAX_TEXT_CHARS]
    if len(raw_text) > MAX_TEXT_CHARS:
        truncated += "\n...(内容已截断)"
    return (
        "你是一个专业的学术文档分析助手。请从以下文档内容中提取关键信息，"
        "输出严格的 JSON 格式，不要输出任何其他内容。\n"
        "提取字段说明：\n"
        "  title: 文档标题（若无则根据内容推断，字符串）\n"
        "  summary: 核心内容摘要（200字以内，字符串）\n"
        "  outline: 主要章节或段落标题列表（字符串数组，最多10条）\n"
        "  key_points: 核心论点或知识点（字符串数组，最多10条，每条<=50字）\n"
        "  data_mentions: 文中提到的数据、实验结果、统计数字（字符串数组，最多8条）\n"
        "  keywords: 关键词（字符串数组，最多8个）\n\n"
        "文档内容如下：\n"
        "---\n"
        f"{truncated}\n"
        "---\n"
        "请输出 JSON，禁止输出除 JSON 以外的任何字符。"
    )


def call_qwen_api(api_key: str, prompt: str) -> dict:
    payload = {
        "model": "qwen-turbo",
        "input": {
            "messages": [
                {"role": "system", "content": "你是一个专业的学术文档分析助手，只输出 JSON。"},
                {"role": "user", "content": prompt},
            ]
        },
        "parameters": {"result_format": "message"},
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        QWEN_ENDPOINT,
        data=data,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"Qwen API HTTP 错误: {e.code} {e.reason}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"Qwen API 网络错误: {e.reason}")

    resp_json = json.loads(body)
    # 提取文本
    text = ""
    try:
        text = resp_json["output"]["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        try:
            text = resp_json["output"]["text"]
        except (KeyError, TypeError):
            pass
    if not text:
        raise RuntimeError("Qwen API 返回内容为空")
    return text


def parse_json_from_text(text: str) -> dict:
    """从可能含有 markdown 代码块的文本中提取 JSON。"""
    # 去掉 markdown 代码块标记
    text = re.sub(r"```(?:json)?\s*", "", text)
    text = text.strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        # 尝试找到第一个 { ... } 块
        match = re.search(r"\{.*\}", text, re.DOTALL)
        if match:
            return json.loads(match.group(0))
        raise


def normalize_result(result: dict) -> dict:
    """确保所有字段存在且类型正确。"""
    def to_str(v, default=""):
        return str(v) if v is not None else default

    def to_str_list(v, max_items=10):
        if isinstance(v, list):
            return [str(x) for x in v if x][:max_items]
        return []

    return {
        "title": to_str(result.get("title")),
        "summary": to_str(result.get("summary")),
        "outline": to_str_list(result.get("outline"), 10),
        "key_points": to_str_list(result.get("key_points"), 10),
        "data_mentions": to_str_list(result.get("data_mentions"), 8),
        "keywords": to_str_list(result.get("keywords"), 8),
    }


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="文档关键信息提取")
    parser.add_argument("--file", required=True, help="文件路径")
    parser.add_argument("--type", required=True, help="文件类型: pdf / docx / txt")
    parser.add_argument("--api-key", required=True, help="通义千问 API Key")
    args = parser.parse_args()

    try:
        raw_text = extract_raw_text(args.file, args.type)
    except Exception as e:
        print(json.dumps({"error": f"文本提取失败: {str(e)}"}), flush=True)
        sys.exit(1)

    if not raw_text.strip():
        print(json.dumps({"error": "文档内容为空，无法提取"}), flush=True)
        sys.exit(1)

    prompt = build_extract_prompt(raw_text)

    try:
        raw_response = call_qwen_api(args.api_key, prompt)
    except Exception as e:
        print(json.dumps({"error": f"AI 提取失败: {str(e)}"}), flush=True)
        sys.exit(1)

    try:
        result_dict = parse_json_from_text(raw_response)
    except Exception as e:
        print(json.dumps({"error": f"AI 返回 JSON 解析失败: {str(e)}", "raw": raw_response[:500]}), flush=True)
        sys.exit(1)

    normalized = normalize_result(result_dict)
    print(json.dumps(normalized, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
