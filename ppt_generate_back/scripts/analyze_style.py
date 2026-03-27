#!/usr/bin/env python3
"""
analyze_style.py — PPT 风格迁移分析脚本（F10）

从上传的参考 .pptx 文件中提取视觉风格规格（StyleSpec），包括：
  - 配色方案（主色 / 背景色 / 强调色 / 文字颜色）
  - 字体信息（标题字体 / 正文字体 / 字号）
  - 布局模式（内容区域比例、标题位置）

输出格式（JSON stdout）：
{
  "palette": {
    "primary":    "#1E3A5F",
    "secondary":  "#2D6A4F",
    "accent":     "#F4A261",
    "background": "#F8F9FA",
    "text_dark":  "#0D1B2A",
    "text_light": "#FFFFFF"
  },
  "typography": {
    "title_font":  "Calibri",
    "body_font":   "Calibri",
    "title_size":  40,
    "body_size":   18
  },
  "layout": {
    "title_position": "top",
    "has_accent_bar": true,
    "content_ratio":  0.75
  },
  "preview_palette": ["#1E3A5F","#2D6A4F","#F4A261","#F8F9FA","#0D1B2A","#FFFFFF"],
  "sample_slides":   2
}
"""

import argparse
import json
import sys
import colorsys
from pathlib import Path
from collections import Counter

try:
    from pptx import Presentation
    from pptx.dml.color import RGBColor
    from pptx.util import Pt
except ImportError as exc:
    print(json.dumps({"error": "python-pptx is required. Install with: pip install python-pptx"}))
    sys.exit(1)


# ---------- helpers ----------------------------------------------------------

def rgb_to_hex(r, g, b):
    return "#{:02X}{:02X}{:02X}".format(int(r), int(g), int(b))


def try_get_rgb(color_obj):
    """Safely try to extract an (r,g,b) tuple from a pptx color object."""
    if color_obj is None:
        return None
    try:
        rgb = color_obj.rgb
        if rgb is not None:
            return (rgb.red, rgb.green, rgb.blue)
    except Exception:
        pass
    return None


def luminance(r, g, b):
    """Relative luminance (0-1)."""
    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def is_dark(r, g, b):
    return luminance(r, g, b) < 0.35


def hex_distance(hex1, hex2):
    """Simple Euclidean distance in RGB space between two hex color strings."""
    def parse(h):
        h = h.lstrip('#')
        return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    r1, g1, b1 = parse(hex1)
    r2, g2, b2 = parse(hex2)
    return ((r1 - r2)**2 + (g1 - g2)**2 + (b1 - b2)**2) ** 0.5


def deduplicate_colors(colors, min_dist=30):
    """Remove near-duplicate colors from a list of hex strings."""
    unique = []
    for c in colors:
        if all(hex_distance(c, u) >= min_dist for u in unique):
            unique.append(c)
    return unique


# ---------- extraction -------------------------------------------------------

def extract_slide_colors(slide):
    """Return list of (r,g,b) tuples found in all shape fills and text in a slide."""
    colors = []

    # Background fill
    try:
        bg = slide.background.fill
        if bg.type is not None:
            rgb = try_get_rgb(bg.fore_color)
            if rgb:
                colors.append(rgb)
    except Exception:
        pass

    for shape in slide.shapes:
        # Shape fill
        try:
            fill = shape.fill
            if fill.type is not None:
                rgb = try_get_rgb(fill.fore_color)
                if rgb:
                    colors.append(rgb)
        except Exception:
            pass

        # Text color
        if getattr(shape, 'has_text_frame', False):
            try:
                for para in shape.text_frame.paragraphs:
                    for run in para.runs:
                        rgb = try_get_rgb(run.font.color)
                        if rgb:
                            colors.append(rgb)
            except Exception:
                pass

    return colors


def extract_fonts(slide):
    """Return (title_font, body_font, title_size_pt, body_size_pt) from a slide."""
    title_font = None
    body_font = None
    title_size = None
    body_size = None

    for shape in slide.shapes:
        if not getattr(shape, 'has_text_frame', False):
            continue
        try:
            ph_type = shape.placeholder_format
        except Exception:
            ph_type = None

        is_title = ph_type is not None and ph_type.idx in (0, 1)  # 0=center title, 1=title

        tf = shape.text_frame
        for para in tf.paragraphs:
            for run in para.runs:
                font_name = run.font.name
                font_size = run.font.size
                if font_name:
                    if is_title and title_font is None:
                        title_font = font_name
                        if font_size:
                            title_size = round(font_size.pt)
                    elif not is_title and body_font is None:
                        body_font = font_name
                        if font_size:
                            body_size = round(font_size.pt)

    return title_font, body_font, title_size, body_size


def analyze_layout(slide):
    """Rough layout analysis: title position and content ratio."""
    slide_w = slide.shapes._spTree.getparent().getparent().slide_width
    slide_h = slide.shapes._spTree.getparent().getparent().slide_height

    title_top_frac = None
    content_shapes = 0
    total_shapes = 0

    for shape in slide.shapes:
        total_shapes += 1
        try:
            ph = shape.placeholder_format
        except Exception:
            ph = None

        if ph is not None and ph.idx in (0, 1):
            try:
                title_top_frac = shape.top / slide_h
            except Exception:
                pass
        else:
            content_shapes += 1

    content_ratio = content_shapes / total_shapes if total_shapes else 0.7

    has_accent_bar = False
    for shape in slide.shapes:
        try:
            fill = shape.fill
            if fill.type is not None:
                rgb = try_get_rgb(fill.fore_color)
                if rgb and not is_dark(*rgb):
                    try:
                        h = shape.height / slide_h
                        w = shape.width / slide_w
                        if (w > 0.5 and h < 0.08) or (h > 0.5 and w < 0.08):
                            has_accent_bar = True
                    except Exception:
                        pass
        except Exception:
            pass

    return {
        "title_position": "top" if (title_top_frac is None or title_top_frac < 0.3) else "center",
        "has_accent_bar": has_accent_bar,
        "content_ratio": round(content_ratio, 2)
    }


# ---------- main analysis ----------------------------------------------------

def analyze(pptx_path: str) -> dict:
    try:
        prs = Presentation(pptx_path)
    except Exception as e:
        return {"error": f"无法读取 PPTX 文件: {e}"}

    slides = list(prs.slides)
    if not slides:
        return {"error": "PPT 文件中没有幻灯片"}

    # Analyze first N slides (cover + content pages)
    sample_count = min(len(slides), 5)
    sample_slides = slides[:sample_count]

    all_colors = []
    title_fonts = []
    body_fonts = []
    title_sizes = []
    body_sizes = []
    layouts = []

    for slide in sample_slides:
        all_colors.extend(extract_slide_colors(slide))
        tf, bf, ts, bs = extract_fonts(slide)
        if tf:
            title_fonts.append(tf)
        if bf:
            body_fonts.append(bf)
        if ts:
            title_sizes.append(ts)
        if bs:
            body_sizes.append(bs)
        layouts.append(analyze_layout(slide))

    # Convert colors to hex and count frequency
    hex_colors = [rgb_to_hex(*c) for c in all_colors if c]
    color_counts = Counter(hex_colors)

    # Filter out pure white and near-black noise
    def is_neutral(h):
        h = h.lstrip('#')
        r, g, b = int(h[0:2],16), int(h[2:4],16), int(h[4:6],16)
        s = max(r,g,b) - min(r,g,b)
        return s < 12  # near-greyscale

    non_neutral = [(c, n) for c, n in color_counts.most_common(40) if not is_neutral(c)]
    all_by_freq = [c for c, _ in color_counts.most_common(40)]

    # Build palette
    dark_colors = []
    light_colors = []
    mid_colors = []
    for hex_c, _ in color_counts.most_common(40):
        h = hex_c.lstrip('#')
        r, g, b = int(h[0:2],16), int(h[2:4],16), int(h[4:6],16)
        lum = luminance(r, g, b)
        if lum < 0.15:
            dark_colors.append(hex_c)
        elif lum > 0.75:
            light_colors.append(hex_c)
        else:
            mid_colors.append(hex_c)

    # Primary: most frequent non-neutral dark color
    primary_candidates = [c for c, _ in non_neutral if is_dark(*[int(c.lstrip('#')[i:i+2],16) for i in (0,2,4)])]
    primary = primary_candidates[0] if primary_candidates else (dark_colors[0] if dark_colors else "#1E3A5F")

    # Background: most frequent light color
    background_candidates = [c for c in all_by_freq if not is_neutral(c) or True]
    bg_light = [c for c in all_by_freq if not is_dark(*[int(c.lstrip('#')[i:i+2],16) for i in (0,2,4)])]
    background = bg_light[0] if bg_light else "#F8F9FA"

    # Accent: mid-tone non-neutral color furthest from primary
    accent = "#F4A261"
    if mid_colors:
        best_dist = 0
        for c in mid_colors[:10]:
            d = hex_distance(c, primary)
            if d > best_dist:
                best_dist = d
                accent = c

    # Secondary: second distinct dark/mid color
    secondary = "#2D6A4F"
    sec_candidates = deduplicate_colors([c for c in (primary_candidates + mid_colors) if c != primary], min_dist=40)
    if sec_candidates:
        secondary = sec_candidates[0]

    # Text colors
    text_dark = dark_colors[0] if dark_colors else "#0D1B2A"
    text_light = "#FFFFFF"
    for c in all_by_freq:
        h = c.lstrip('#')
        r, g, b = int(h[0:2],16), int(h[2:4],16), int(h[4:6],16)
        if luminance(r,g,b) > 0.85:
            text_light = c
            break

    palette = {
        "primary":    primary,
        "secondary":  secondary,
        "accent":     accent,
        "background": background,
        "text_dark":  text_dark,
        "text_light": text_light,
    }

    # Typography
    def most_common(lst, default):
        if not lst:
            return default
        return Counter(lst).most_common(1)[0][0]

    typography = {
        "title_font": most_common(title_fonts, "Calibri"),
        "body_font":  most_common(body_fonts, "Calibri"),
        "title_size": most_common(title_sizes, 36) or 36,
        "body_size":  most_common(body_sizes, 18) or 18,
    }

    # Layout: aggregate across sample slides
    has_bar = any(l["has_accent_bar"] for l in layouts)
    avg_ratio = round(sum(l["content_ratio"] for l in layouts) / len(layouts), 2)
    title_pos_counts = Counter(l["title_position"] for l in layouts)
    title_pos = title_pos_counts.most_common(1)[0][0]

    layout = {
        "title_position": title_pos,
        "has_accent_bar": has_bar,
        "content_ratio":  avg_ratio,
    }

    preview_palette = list(dict.fromkeys([
        palette["primary"], palette["secondary"], palette["accent"],
        palette["background"], palette["text_dark"], palette["text_light"]
    ]))

    return {
        "palette":         palette,
        "typography":      typography,
        "layout":          layout,
        "preview_palette": preview_palette,
        "sample_slides":   sample_count,
    }


# ---------- CLI --------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Analyze PPTX style and output StyleSpec JSON")
    parser.add_argument("--input", required=True, help="Path to the reference .pptx file")
    args = parser.parse_args()

    if not Path(args.input).exists():
        print(json.dumps({"error": f"文件不存在: {args.input}"}))
        sys.exit(1)

    result = analyze(args.input)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
