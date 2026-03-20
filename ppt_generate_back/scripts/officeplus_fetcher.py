#!/usr/bin/env python3
"""
OfficePLUS 模板导入工具 v2
officeplus.cn 已迁移到 Next.js + 独立认证 API。
- 无 token：只能通过详情页 URL / ID 导入单个模板（抓取元数据）
- 有 token (optoken cookie)：调用 api.officeplus.cn 接口搜索模板列表

用法：
  # 搜索（需 cookie 中含 optoken，或留空则返回提示）
  python3 officeplus_fetcher.py search --keyword 商务 --page 1 --page_size 20 [--cookie "optoken=xxx"]

  # 抓取单个模板信息
  python3 officeplus_fetcher.py info --url https://www.officeplus.cn/PPT/detail/PptContent-348931/

  # 下载并写入 catalog
  python3 officeplus_fetcher.py download --id 348931 --catalog config/templates.json \
      --templates_dir assets/templates --thumbnails_dir assets/template_thumbnails \
      [--cookie "optoken=xxx"]
"""
from __future__ import annotations

import argparse
import html as html_module
import json
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional
from urllib.parse import urlencode, urlparse, parse_qs, quote

SITE_BASE = "https://www.officeplus.cn"
API_BASE  = "https://api.officeplus.cn"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
TIMEOUT = 30

# ── 辅助函数 ─────────────────────────────────────────────────────────────────

def curl_get(url: str, cookie: str = "", extra_headers: list[str] | None = None,
             accept: str = "*/*") -> str:
    cmd = [
        "curl", "-sSL",
        "--max-time", str(TIMEOUT),
        "-H", f"User-Agent: {UA}",
        "-H", f"Accept: {accept}",
        "-H", "Accept-Language: zh-CN,zh;q=0.9",
        "-H", "Referer: https://www.officeplus.cn/",
        "-H", "Origin: https://www.officeplus.cn",
    ]
    if cookie:
        cmd += ["-H", f"Cookie: {cookie}"]
    if extra_headers:
        for h in extra_headers:
            cmd += ["-H", h]
    cmd.append(url)
    r = subprocess.run(cmd, capture_output=True, timeout=TIMEOUT + 5)
    if r.returncode != 0:
        raise RuntimeError(f"curl failed: {r.stderr.decode(errors='replace')[:200]}")
    return r.stdout.decode("utf-8", errors="replace")


def curl_download(url: str, dest: Path, cookie: str = "") -> bool:
    cmd = [
        "curl", "-sSL", "--max-time", "120",
        "-o", str(dest),
        "-H", f"User-Agent: {UA}",
        "-H", "Referer: https://www.officeplus.cn/",
        "-H", "Origin: https://www.officeplus.cn",
    ]
    if cookie:
        cmd += ["-H", f"Cookie: {cookie}"]
    cmd.append(url)
    r = subprocess.run(cmd, capture_output=True, timeout=130)
    if r.returncode != 0:
        return False
    if not dest.exists() or dest.stat().st_size < 5000:
        return False
    with open(dest, "rb") as f:
        header = f.read(2)
    if header != b"PK":
        dest.unlink(missing_ok=True)
        return False
    return True


def get_build_id(cookie: str = "") -> str:
    """从首页获取 Next.js buildId。"""
    try:
        html_text = curl_get(SITE_BASE + "/PPT/template/", cookie=cookie)
        m = re.search(r'"buildId":"([^"]+)"', html_text)
        if m:
            return m.group(1)
    except Exception:
        pass
    return ""


def extract_optoken(cookie: str) -> str:
    """从 Cookie 字符串中提取 optoken 的值。"""
    m = re.search(r'(?:^|;)\s*optoken=([^;]+)', cookie)
    if m:
        return m.group(1).strip()
    return ""


def id_from_url(url: str) -> Optional[str]:
    """从 URL 提取数字 ID。"""
    m = re.search(r'PptContent-(\d+)', url)
    if m:
        return m.group(1)
    m = re.search(r'(\d{5,})', url)
    if m:
        return m.group(1)
    return None


# ── API 调用（需 optoken） ────────────────────────────────────────────────────

def api_search_with_token(optoken: str, keyword: str, page: int, page_size: int,
                           sub_category_id: str = "d0f06dda-fe75-4534-8a4d-46b91b2aefd4") -> dict:
    """
    使用 optoken 调用 api.officeplus.cn 搜索 PPT 模板。
    subCategoryId 固定为 PPT 模板分类。
    """
    params = {
        "subCategoryId": sub_category_id,
        "pageIndex": page,
        "pageSize": page_size,
        "orderBy": "Total",
        "paymentMethod": "2",  # 免费和会员专享
    }
    if keyword.strip():
        params["keywords"] = keyword.strip()

    # 尝试几种已知路径格式
    candidate_paths = [
        "/api/template/v2/list",
        "/api/template/list",
        "/template/list",
        "/api/ppt/template/list",
    ]

    auth_headers = [
        f"Authorization: Bearer {optoken}",
        "x-ms-client-application: OfficePlusWeb",
        "Accept: application/json",
    ]

    for path in candidate_paths:
        url = API_BASE + path + "?" + urlencode(params)
        try:
            resp_text = curl_get(url, extra_headers=auth_headers)
            if resp_text.strip().startswith("{") or resp_text.strip().startswith("["):
                data = json.loads(resp_text)
                # 检查是否有模板数据
                if isinstance(data, dict) and ("data" in data or "items" in data or "list" in data):
                    return {"ok": True, "path": path, "data": data}
        except Exception:
            continue

    return {"ok": False, "error": "未找到可用的 API 端点，请确认 cookie 中包含有效的 optoken"}


def parse_next_data_templates(html_text: str) -> list[dict]:
    """从 Next.js SSR 页面提取模板相关的静态数据（热词推荐等）。"""
    items = []
    try:
        m = re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', html_text, re.S)
        if not m:
            return []
        data = json.loads(m.group(1))
        props = data.get("props", {}).get("pageProps", {})

        # 提取热门搜索关键词（作为参考）
        hot = props.get("hotSearchKeywords", [])
        for kw in hot:
            if isinstance(kw, dict) and "title" in kw:
                items.append({
                    "id": "",
                    "name": kw["title"],
                    "preview_image": kw.get("image", ""),
                    "page_url": SITE_BASE + kw.get("path", "/PPT/template/"),
                    "type": "hotword",
                })
    except Exception:
        pass
    return items


# ── 抓取详情页元数据 ──────────────────────────────────────────────────────────

def extract_file_name_from_detail(html_text: str) -> str:
    """从模板详情页 __NEXT_DATA__ 中提取 fileName（Azure Blob 文件名）。"""
    m = re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', html_text, re.S)
    if m:
        try:
            data = json.loads(m.group(1))
            detail = data.get("props", {}).get("pageProps", {}).get("detail", {})
            return detail.get("fileName", "")
        except Exception:
            pass
    # 回退：正则直接搜
    m2 = re.search(r'"fileName"\s*:\s*"([^"]+\.pptx)"', html_text)
    if m2:
        return m2.group(1)
    return ""


def get_download_url_with_token(optoken: str, file_name: str, tmpl_id: str) -> str:
    """
    用 optoken 调用 api.officeplus.cn 获取文件下载 URL（SAS URL 或直接流）。
    尝试多种已知 API 格式，返回可下载的 URL 或空字符串。
    """
    auth_headers = [
        f"Authorization: Bearer {optoken}",
        "x-ms-client-application: OfficePlusWeb",
        "Accept: application/json, */*",
    ]

    # 候选接口路径（按可能性排序）
    candidates = [
        f"{API_BASE}/api/template/download?fileName={quote(file_name)}",
        f"{API_BASE}/api/template/download?id=PptContent-{tmpl_id}&fileName={quote(file_name)}",
        f"{API_BASE}/api/template/v1/download?fileName={quote(file_name)}",
        f"{API_BASE}/api/template/file?fileName={quote(file_name)}",
        f"{API_BASE}/api/file/download?fileName={quote(file_name)}",
        f"{API_BASE}/api/content/download?fileName={quote(file_name)}",
        f"{API_BASE}/api/template/getDownloadUrl?fileName={quote(file_name)}",
        f"{API_BASE}/api/template/sas?fileName={quote(file_name)}",
    ]

    for url in candidates:
        try:
            cmd = [
                "curl", "-sI", "--max-time", "10", "--no-location",
                "-H", f"User-Agent: {UA}",
                "-H", "Origin: https://www.officeplus.cn",
                "-H", "Referer: https://www.officeplus.cn/",
            ] + [item for h in auth_headers for item in ["-H", h]] + [url]

            r = subprocess.run(cmd, capture_output=True, timeout=15)
            resp = r.stdout.decode("utf-8", errors="replace")
            statuses = re.findall(r"HTTP/\S+\s+(\d+)", resp)
            final_status = int(statuses[-1]) if statuses else 0

            if final_status in (200, 206):
                # 直接可下载
                ctype = re.search(r"content-type:\s*([^\r\n]+)", resp, re.I)
                if ctype and ("openxml" in ctype.group(1).lower() or
                              "application/zip" in ctype.group(1).lower() or
                              "octet-stream" in ctype.group(1).lower()):
                    return url

            elif final_status in (301, 302, 307, 308):
                location = re.search(r"location:\s*([^\r\n]+)", resp, re.I)
                if location:
                    redirect_url = location.group(1).strip()
                    if redirect_url.startswith("http") and ".pptx" in redirect_url.lower():
                        return redirect_url
        except Exception:
            continue

    return ""


def download_with_token(optoken: str, file_name: str, tmpl_id: str,
                        dest: Path, cookie: str = "") -> bool:
    """
    尝试多种方式下载 pptx 文件：
    1. 用 optoken 调用 API 获取下载 URL 再下载
    2. 直接带 cookie 访问详情页并跟随下载流程
    """
    auth_headers = [
        f"Authorization: Bearer {optoken}",
        "x-ms-client-application: OfficePlusWeb",
    ]

    # 方法1：先用 HEAD 检测 API 路径，再下载
    download_candidates = [
        f"{API_BASE}/api/template/download?fileName={quote(file_name)}",
        f"{API_BASE}/api/template/download?id=PptContent-{tmpl_id}&fileName={quote(file_name)}",
        f"{API_BASE}/api/template/v1/download?fileName={quote(file_name)}",
        f"{API_BASE}/api/template/file?fileName={quote(file_name)}",
        f"{API_BASE}/api/file/download?fileName={quote(file_name)}",
    ]

    for url in download_candidates:
        # 先用 HEAD+redirect 检测，避免下载大量 HTML 错误页
        head_cmd = [
            "curl", "-sI", "--max-time", "10", "-L",
            "-H", f"User-Agent: {UA}",
            "-H", "Origin: https://www.officeplus.cn",
            "-H", "Referer: https://www.officeplus.cn/",
            "-w", "\nFINAL_STATUS:%{http_code}",
        ] + [item for h in auth_headers for item in ["-H", h]]
        if cookie:
            head_cmd += ["-H", f"Cookie: {cookie}"]
        head_cmd.append(url)

        try:
            hr = subprocess.run(head_cmd, capture_output=True, timeout=15)
            head_resp = hr.stdout.decode("utf-8", errors="replace")
            final_status = ""
            for line in head_resp.split("\n"):
                if line.startswith("FINAL_STATUS:"):
                    final_status = line.split(":", 1)[1].strip()
            ctype_m = re.search(r"content-type:\s*([^\r\n]+)", head_resp, re.I)
            ctype = ctype_m.group(1).strip() if ctype_m else ""

            is_file = (final_status == "200" and
                       any(k in ctype.lower() for k in ("openxml", "zip", "octet", "pptx")))
            if not is_file:
                continue
        except Exception:
            continue

        cmd = [
            "curl", "-sSL", "--max-time", "120",
            "-o", str(dest),
            "-H", f"User-Agent: {UA}",
            "-H", "Origin: https://www.officeplus.cn",
            "-H", "Referer: https://www.officeplus.cn/",
        ] + [item for h in auth_headers for item in ["-H", h]]
        if cookie:
            cmd += ["-H", f"Cookie: {cookie}"]
        cmd.append(url)

        try:
            r = subprocess.run(cmd, capture_output=True, timeout=130)
            if dest.exists() and dest.stat().st_size > 10000:
                with open(dest, "rb") as f:
                    if f.read(2) == b"PK":
                        return True
            if dest.exists():
                dest.unlink(missing_ok=True)
        except Exception:
            continue

    # 方法2：用 cookie 直接访问 www.officeplus.cn（跟随重定向）
    if cookie:
        www_candidates = [
            f"{SITE_BASE}/api/template/download?id=PptContent-{tmpl_id}",
            f"{SITE_BASE}/api/ppt/download/{tmpl_id}/",
            f"{SITE_BASE}/api/content/download?id=PptContent-{tmpl_id}&fileName={quote(file_name)}",
        ]
        for url in www_candidates:
            # HEAD 检测
            try:
                hr = subprocess.run([
                    "curl", "-sI", "--max-time", "10", "-L",
                    "-H", f"User-Agent: {UA}", "-H", f"Cookie: {cookie}",
                    "-w", "\nFINAL_STATUS:%{http_code}", url
                ], capture_output=True, timeout=15)
                head_resp = hr.stdout.decode("utf-8", errors="replace")
                final_status = ""
                for line in head_resp.split("\n"):
                    if line.startswith("FINAL_STATUS:"): final_status = line.split(":",1)[1].strip()
                ctype_m = re.search(r"content-type:\s*([^\r\n]+)", head_resp, re.I)
                ctype = ctype_m.group(1).strip() if ctype_m else ""
            except Exception:
                pass

            ok = curl_download(url, dest, cookie)
            if ok:
                return True

    return False


def extract_meta_from_detail(html_text: str, tmpl_id: str) -> dict:
    """从模板详情页 HTML 提取元数据（支持新版 Next.js 格式）。"""
    name = ""
    preview_image = ""
    description = ""
    tags: list[str] = []

    # 优先从 __NEXT_DATA__ 提取
    m = re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', html_text, re.S)
    if m:
        try:
            data = json.loads(m.group(1))
            props = data.get("props", {}).get("pageProps", {})

            # 名称
            name = props.get("title", props.get("name", ""))
            if not name:
                # 深度搜索
                for key in ("templateName", "name", "title"):
                    def _find(obj, k):
                        if isinstance(obj, dict):
                            if k in obj and isinstance(obj[k], str) and len(obj[k]) > 1:
                                return obj[k]
                            for v in obj.values():
                                r = _find(v, k)
                                if r:
                                    return r
                        elif isinstance(obj, list):
                            for item in obj:
                                r = _find(item, k)
                                if r:
                                    return r
                        return ""
                    name = _find(props, key)
                    if name:
                        break

            # 封面图
            preview_image = props.get("cover", props.get("previewImage", props.get("coverUrl", "")))
            if not preview_image:
                def _find_cover(obj):
                    if isinstance(obj, dict):
                        for k in ("cover", "coverUrl", "thumbnail", "previewImage", "image"):
                            v = obj.get(k, "")
                            if isinstance(v, str) and (v.startswith("http") or v.startswith("//")):
                                return v
                        for v in obj.values():
                            r = _find_cover(v)
                            if r:
                                return r
                    elif isinstance(obj, list):
                        for item in obj:
                            r = _find_cover(item)
                            if r:
                                return r
                    return ""
                preview_image = _find_cover(props)

            description = props.get("description", "")
            if not description:
                description = data.get("props", {}).get("pageProps", {}).get("seoDescription", "")

            # 标签
            raw_tags = props.get("tags", [])
            if isinstance(raw_tags, list):
                for t in raw_tags:
                    if isinstance(t, str):
                        tags.append(t)
                    elif isinstance(t, dict):
                        tags.append(t.get("title", t.get("name", "")))
        except Exception:
            pass

    # 回退到 og meta 标签
    if not name:
        m2 = re.search(r'property="og:title"\s+content="([^"]+)"', html_text)
        if m2:
            name = html_module.unescape(m2.group(1)).strip()
            name = re.sub(r'\s*[-–]\s*OfficePLUS.*$', '', name, flags=re.I).strip()

    if not preview_image:
        m2 = re.search(r'property="og:image"\s+content="([^"]+)"', html_text)
        if m2:
            preview_image = m2.group(1).strip()

    if not description:
        m2 = re.search(r'(?:property="og:description"|name="description")\s+content="([^"]+)"', html_text)
        if m2:
            description = html_module.unescape(m2.group(1)).strip()

    if preview_image.startswith("//"):
        preview_image = "https:" + preview_image

    # 清理名称中的网站后缀
    if name:
        name = re.sub(r'\s*[|｜]\s*PPT模板下载.*$', '', name).strip()
        name = re.sub(r'\s*[-–]\s*OfficePLUS.*$', '', name, flags=re.I).strip()

    return {
        "id_str": tmpl_id,
        "name": name or f"OfficePLUS 模板 {tmpl_id}",
        "description": description,
        "preview_image": preview_image,
        "tags": [t for t in tags if t][:6],
        "page_url": f"{SITE_BASE}/PPT/detail/PptContent-{tmpl_id}/",
        "download_url": f"{SITE_BASE}/api/template/download.ashx?id={tmpl_id}",
    }


# ── 子命令：info ──────────────────────────────────────────────────────────────

def cmd_info(args) -> int:
    url = args.url or ""
    tmpl_id = args.id or ""

    if url and not tmpl_id:
        tmpl_id = id_from_url(url) or ""
        if not tmpl_id:
            print(json.dumps({"error": f"无法从 URL 提取模板 ID: {url}"}))
            return 1

    if not tmpl_id:
        print(json.dumps({"error": "请提供 --url 或 --id"}))
        return 1

    page_url = url or f"{SITE_BASE}/PPT/detail/PptContent-{tmpl_id}/"
    try:
        html_text = curl_get(page_url, cookie=args.cookie or "")
    except Exception as e:
        print(json.dumps({"error": str(e)}))
        return 1

    meta = extract_meta_from_detail(html_text, tmpl_id)
    print(json.dumps(meta, ensure_ascii=False))
    return 0


# ── 子命令：search ────────────────────────────────────────────────────────────

def cmd_search(args) -> int:
    cookie = args.cookie or ""
    keyword = (args.keyword or "").strip()
    page = max(1, args.page)
    page_size = min(40, max(1, args.page_size))

    optoken = extract_optoken(cookie)

    if optoken:
        # 有 token，调用真实 API
        result = api_search_with_token(optoken, keyword, page, page_size)
        if result.get("ok"):
            raw = result["data"]
            # 统一化输出
            items = []
            # 常见字段名称尝试
            raw_items = (raw.get("data") or raw.get("items") or
                         raw.get("list") or raw.get("value") or [])
            if isinstance(raw_items, dict):
                raw_items = raw_items.get("items", raw_items.get("list", []))

            for t in raw_items:
                if not isinstance(t, dict):
                    continue
                tmpl_id = str(t.get("id", t.get("templateId", t.get("contentId", ""))))
                cover = t.get("cover", t.get("coverUrl", t.get("thumbnail", t.get("previewImage", ""))))
                if cover and cover.startswith("//"):
                    cover = "https:" + cover
                name = t.get("name", t.get("title", t.get("templateName", f"模板 {tmpl_id}")))
                items.append({
                    "id": tmpl_id,
                    "name": name,
                    "preview_image": cover,
                    "page_url": f"{SITE_BASE}/PPT/detail/PptContent-{tmpl_id}/",
                    "tags": [tg.get("title", "") for tg in t.get("tags", []) if isinstance(tg, dict)][:4],
                    "isFree": t.get("isFree", True),
                })
            print(json.dumps({"total": len(items), "page": page, "items": items,
                              "source": "api"}, ensure_ascii=False))
            return 0
        else:
            # API 失败，返回错误信息
            print(json.dumps({"total": 0, "page": page, "items": [],
                              "error": result.get("error", "API 调用失败"),
                              "needCookie": True,
                              "hint": "请在 OfficePLUS 网站登录后，从浏览器 DevTools > Network 中复制 Cookie 请求头（需包含 optoken 字段）"},
                             ensure_ascii=False))
            return 0
    else:
        # 无 token，返回提示和使用说明
        print(json.dumps({
            "total": 0,
            "page": page,
            "items": [],
            "needCookie": True,
            "hint": "请在右上角输入 OfficePLUS 登录 Cookie（需包含 optoken 字段）以搜索模板列表。\n\n获取方法：\n1. 在浏览器打开 officeplus.cn 并登录\n2. 打开开发者工具（F12）→ Network 标签\n3. 刷新页面，找任意请求，复制请求头中的 Cookie 值\n4. 粘贴到上方 Cookie 输入框",
        }, ensure_ascii=False))
        return 0


# ── 子命令：download ──────────────────────────────────────────────────────────

def cmd_download(args) -> int:
    tmpl_id = (args.id or "").strip()
    if not tmpl_id and args.url:
        tmpl_id = id_from_url(args.url) or ""
    if not tmpl_id:
        print(json.dumps({"error": "请提供 --id 或 --url"}))
        return 1

    catalog_path = Path(args.catalog)
    templates_dir = Path(args.templates_dir)
    thumbnails_dir = Path(args.thumbnails_dir)
    cookie = args.cookie or ""

    templates_dir.mkdir(parents=True, exist_ok=True)
    thumbnails_dir.mkdir(parents=True, exist_ok=True)

    custom_id = args.custom_id or f"op-{tmpl_id}"
    local_filename = f"{custom_id}.pptx"
    local_path = templates_dir / local_filename
    thumbnail_path = thumbnails_dir / f"{custom_id}.jpg"

    result: dict = {"success": False, "id": custom_id, "tmplId": tmpl_id}

    optoken = extract_optoken(cookie)

    # 1. 抓取详情页元数据（含 fileName）
    page_url = args.url or f"{SITE_BASE}/PPT/detail/PptContent-{tmpl_id}/"
    html_text = ""
    try:
        html_text = curl_get(page_url, cookie=cookie)
        meta = extract_meta_from_detail(html_text, tmpl_id)
    except Exception as e:
        meta = {
            "id_str": tmpl_id,
            "name": f"OfficePLUS {tmpl_id}",
            "description": "",
            "preview_image": "",
            "tags": [],
            "page_url": page_url,
        }
        result["warn"] = f"元数据抓取失败: {e}"

    # 提取 Azure Blob fileName（用于下载）
    azure_file_name = extract_file_name_from_detail(html_text) if html_text else ""

    # 2. 下载缩略图
    if meta.get("preview_image") and not thumbnail_path.exists():
        try:
            curl_download(meta["preview_image"], thumbnail_path, cookie)
        except Exception:
            pass

    # 3. 下载 pptx
    if local_path.exists() and local_path.stat().st_size > 5000:
        with open(local_path, "rb") as f:
            if f.read(2) == b"PK":
                result["skipped"] = True

    if not result.get("skipped"):
        downloaded = False

        # 方式 A：有 optoken + fileName → 调用认证 API 下载
        if optoken and azure_file_name:
            try:
                downloaded = download_with_token(optoken, azure_file_name, tmpl_id, local_path, cookie)
                if downloaded:
                    result["downloadMethod"] = "api_token"
            except Exception as e:
                result["downloadWarn"] = f"token 下载失败: {e}"

        # 方式 B：有 cookie，直接带 cookie 请求（无 optoken 时也试一次）
        if not downloaded and cookie:
            try:
                downloaded = download_with_token("", azure_file_name or "", tmpl_id, local_path, cookie)
                if downloaded:
                    result["downloadMethod"] = "cookie_only"
            except Exception:
                pass

        if not downloaded:
            result["downloadFailed"] = True
            hint_parts = ["pptx 下载失败。"]
            if not cookie:
                hint_parts.append("请在导入面板提供 OfficePLUS 登录 Cookie（需包含 optoken 字段）后重试。")
            else:
                hint_parts.append("Cookie 可能已过期或该模板需要付费会员权限。")
            hint_parts.append(f"\n\n也可手动在 OfficePLUS 网站下载 pptx 文件，放入 assets/templates/ 目录并命名为 {local_filename}")
            result["downloadHint"] = "".join(hint_parts)
            if azure_file_name:
                result["azureFileName"] = azure_file_name

    # 4. 更新 catalog
    catalog: list[dict] = []
    if catalog_path.exists():
        try:
            with open(catalog_path, encoding="utf-8") as f:
                catalog = json.load(f)
        except Exception:
            catalog = []

    existing_idx = next((i for i, t in enumerate(catalog) if t.get("id") == custom_id), -1)

    try:
        rel_local = str(local_path.resolve().relative_to(catalog_path.resolve().parent))
    except ValueError:
        rel_local = str(local_path.resolve())

    has_local = local_path.exists() and local_path.stat().st_size > 5000

    entry: dict = {
        "id": custom_id,
        "name": meta.get("name", custom_id),
        "provider": "OfficePLUS",
        "provider_url": "https://www.officeplus.cn",
        "description": meta.get("description", ""),
        "preview_image": "",
        "download_url": meta.get("page_url", page_url),
        "license": "OfficePLUS 免费模板",
        "tags": meta.get("tags", []),
        "theme": {
            "primary_color": "#1e293b",
            "secondary_color": "#334155",
            "accent_color": "#6366f1",
            "background_image": ""
        },
        "local_file": rel_local if has_local else "",
    }

    if existing_idx >= 0:
        catalog[existing_idx] = entry
    else:
        catalog.append(entry)

    with open(catalog_path, "w", encoding="utf-8") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=2)

    result["success"] = True
    result["entry"] = entry
    result["hasLocalFile"] = has_local
    if thumbnail_path.exists():
        result["thumbnailSaved"] = str(thumbnail_path)

    print(json.dumps(result, ensure_ascii=False))
    return 0


# ── 主入口 ────────────────────────────────────────────────────────────────────

def main() -> int:
    parser = argparse.ArgumentParser(description="OfficePLUS 模板导入工具")
    sub = parser.add_subparsers(dest="cmd")

    p_info = sub.add_parser("info")
    p_info.add_argument("--url", default="")
    p_info.add_argument("--id", default="")
    p_info.add_argument("--cookie", default="")

    p_search = sub.add_parser("search")
    p_search.add_argument("--keyword", default="")
    p_search.add_argument("--tag", default="")
    p_search.add_argument("--page", type=int, default=1)
    p_search.add_argument("--page_size", type=int, default=20)
    p_search.add_argument("--cookie", default="")

    p_dl = sub.add_parser("download")
    p_dl.add_argument("--id", default="")
    p_dl.add_argument("--url", default="")
    p_dl.add_argument("--custom_id", default="")
    p_dl.add_argument("--catalog", required=True)
    p_dl.add_argument("--templates_dir", required=True)
    p_dl.add_argument("--thumbnails_dir", required=True)
    p_dl.add_argument("--cookie", default="")

    args = parser.parse_args()
    if args.cmd == "info":
        return cmd_info(args)
    elif args.cmd == "search":
        return cmd_search(args)
    elif args.cmd == "download":
        return cmd_download(args)
    else:
        parser.print_help()
        return 1


if __name__ == "__main__":
    sys.exit(main())
