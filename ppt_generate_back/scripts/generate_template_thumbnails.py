#!/usr/bin/env python3
"""
从 assets/templates/*.pptx 生成首页缩略图到 assets/template_thumbnails/{id}.png。
依赖: LibreOffice (soffice --headless) 或 unoconv。
用法: 在 ppt_generate_back 目录下执行
  python3 scripts/generate_template_thumbnails.py
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
BACKEND_ROOT = SCRIPT_DIR.parent
TEMPLATES_DIR = BACKEND_ROOT / "assets" / "templates"
THUMBNAILS_DIR = BACKEND_ROOT / "assets" / "template_thumbnails"
# officeplus-01.pptx -> officeplus-01
ID_PATTERN = re.compile(r"^officeplus-(\d{2})\.pptx$", re.I)


def find_soffice() -> str | None:
    for name in ("soffice", "libreoffice"):
        path = shutil.which(name)
        if path:
            return path
    return None


def generate_with_soffice(pptx_path: Path, out_dir: Path) -> Path | None:
    """Convert first slide to PNG using LibreOffice. Returns path to PNG or None."""
    soffice = find_soffice()
    if not soffice:
        return None
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = pptx_path.stem
    expected = out_dir / f"{stem}.png"
    try:
        subprocess.run(
            [
                soffice,
                "--headless",
                "--convert-to", "png",
                "--outdir", str(out_dir),
                str(pptx_path),
            ],
            check=True,
            capture_output=True,
            timeout=90,
        )
        if expected.exists():
            return expected
        for p in (out_dir / f"{stem}-1.png", out_dir / f"{stem}.png"):
            if p.exists():
                if p != expected:
                    shutil.move(str(p), str(expected))
                return expected
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return None


def main() -> int:
    if not TEMPLATES_DIR.is_dir():
        print("Templates dir not found:", TEMPLATES_DIR, file=sys.stderr)
        return 1
    THUMBNAILS_DIR.mkdir(parents=True, exist_ok=True)
    count = 0
    for path in sorted(TEMPLATES_DIR.glob("*.pptx")):
        m = ID_PATTERN.match(path.name)
        if not m:
            continue
        tid = path.stem  # officeplus-01
        out_png = THUMBNAILS_DIR / f"{tid}.png"
        if out_png.exists() and out_png.stat().st_size > 0:
            print(f"Skip (exists): {tid}")
            count += 1
            continue
        first_png = generate_with_soffice(path, THUMBNAILS_DIR)
        if first_png and first_png.exists():
            shutil.move(str(first_png), str(out_png))
            print(f"OK: {tid}")
            count += 1
        else:
            print(f"FAIL: {tid} (LibreOffice not found or conversion failed)", file=sys.stderr)
    print("Thumbnails:", count, "in", THUMBNAILS_DIR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
