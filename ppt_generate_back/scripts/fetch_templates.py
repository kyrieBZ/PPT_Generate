#!/usr/bin/env python3
"""
将 20 个免费 PPT 模板下载到 assets/templates/，命名为 officeplus-01.pptx .. officeplus-20.pptx。
模板来源：GitHub 上可直链下载的 .pptx/.potx（potx 会以 .pptx 形式保存，兼容 PowerPoint）。
使用 curl 以提升大文件下载稳定性。
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

# 20 个直链（均为免费可商用或教育用途的模板）
TEMPLATE_URLS = [
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/160363-cactus-template-16x9.pptx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Blue%20bookstack%20presentation%20(widescreen).potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Bring%20your%20presentations%20to%20life%20with%203D.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Chalkboard%20education%20presentation%20(widescreen).potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Geometric%20presentation.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Organic%20presentation.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Watercolor%20presentation%20(widescreen).potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Educational%20subjects%20presentation%2C%20chalkboard%20illustrations%20design%20(widescreen).potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Math%20education%20presentation%20with%20Pi%20(widescreen).potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Student%20scientific%20report%20presentation.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Lab%20safety.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Double%20helix%20DNA%20graphic.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Dad's%20tie%20design%20slides.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Rules%20design%20slides.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/TF00951641.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/TF16411253.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/TF55661986.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/TF66687569.potx",
    "https://raw.githubusercontent.com/Lian0123/Lian-Free-PPT-Template/master/%5B001%5D%E6%B7%A1%E8%97%8D%E7%A2%8E%E5%B9%95/%E6%B7%A1%E8%97%8D%E7%A2%8E%E5%B9%95.potx",
    "https://raw.githubusercontent.com/smasongarrison/Microsoft-Templates/master/Dad's%20tie%20design%20slides.potx",  # 20: 备用（GBIF 直链常超时）
]

OUTPUT_DIR = Path(__file__).resolve().parent.parent / "assets" / "templates"
CURL_TIMEOUT = 300


def main() -> int:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for i, url in enumerate(TEMPLATE_URLS, start=1):
        out_name = f"officeplus-{i:02d}.pptx"
        out_path = OUTPUT_DIR / out_name
        print(f"[{i}/20] {out_name} <- {url[:60]}...")
        try:
            if out_path.is_file() and out_path.stat().st_size >= 5000:
                with open(out_path, "rb") as f:
                    if f.read(2) == b"PK":
                        print(f"  skip (already exists, valid)")
                        continue
            for attempt in range(3):
                r = subprocess.run(
                    [
                        "curl",
                        "-sSL",
                        "-o",
                        str(out_path),
                        "-w",
                        "%{http_code} %{size_download}",
                        "--max-time",
                        str(CURL_TIMEOUT),
                        "-H",
                        "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
                        url,
                    ],
                    capture_output=True,
                    text=True,
                    timeout=CURL_TIMEOUT + 10,
                )
                if r.returncode != 0 and attempt < 2:
                    print(f"  retry {attempt+2}/3...")
                    continue
                break
            if not out_path.is_file() or out_path.stat().st_size < 5000:
                print(f"  WARN: file missing or too small, skip")
                if out_path.is_file():
                    out_path.unlink()
                continue
            with open(out_path, "rb") as f:
                if f.read(2) != b"PK":
                    print(f"  WARN: not pptx/potx (no PK header), skip")
                    out_path.unlink()
                    continue
            print(f"  OK {out_path.stat().st_size} bytes")
        except subprocess.TimeoutExpired:
            print(f"  FAIL: timeout")
            if out_path.is_file():
                out_path.unlink()
            return 1
        except Exception as e:
            print(f"  FAIL: {e}")
            return 1
    print("Done. Templates in:", OUTPUT_DIR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
