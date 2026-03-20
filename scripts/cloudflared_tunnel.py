#!/usr/bin/env python3
"""
启动 cloudflared 快速隧道，将生成的公网 URL 写入 config.json 的 s3.public_endpoint。
用法：在项目根目录执行  python3 scripts/cloudflared_tunnel.py
      或  ./scripts/cloudflared_tunnel.py
"""

import json
import re
import subprocess
import sys
from pathlib import Path

# 项目根目录（脚本所在目录的上一级）
PROJECT_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = PROJECT_ROOT / "ppt_generate_back" / "config" / "config.json"
TUNNEL_URL_PATTERN = re.compile(r"https://[a-zA-Z0-9][-a-zA-Z0-9]*\.trycloudflare\.com")


def main():
    if not CONFIG_PATH.exists():
        print(f"错误: 配置文件不存在 {CONFIG_PATH}", file=sys.stderr)
        sys.exit(1)

    print("正在启动 cloudflared 隧道 (--url http://localhost:9000) ...")
    proc = subprocess.Popen(
        ["cloudflared", "tunnel", "--url", "http://localhost:9000"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    public_url = None
    try:
        for line in proc.stdout:
            line = line.strip()
            print(line)
            if not public_url:
                match = TUNNEL_URL_PATTERN.search(line)
                if match:
                    public_url = match.group(0)
                    print(f"\n已解析到公网地址: {public_url}")
                    update_config(public_url)
    except KeyboardInterrupt:
        proc.terminate()
        proc.wait()
        print("\n隧道已停止。")
        sys.exit(0)

    proc.wait()
    sys.exit(proc.returncode)


def update_config(public_endpoint: str) -> None:
    """将 public_endpoint 写入 config.json 的 s3.public_endpoint。"""
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        config = json.load(f)
    if "s3" not in config:
        config["s3"] = {}
    config["s3"]["public_endpoint"] = public_endpoint
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(config, f, ensure_ascii=False, indent=2)
    print(f"已更新 {CONFIG_PATH} 中 s3.public_endpoint = {public_endpoint}")


if __name__ == "__main__":
    main()
