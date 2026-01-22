#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path
import zipfile

try:
    from pptx import Presentation
    from pptx.enum.dml import MSO_COLOR_TYPE
    from pptx.enum.shapes import PP_PLACEHOLDER
except ImportError as exc:
    print("python-pptx is required. Install with: pip install python-pptx", file=sys.stderr)
    raise


def delete_slide(pres, index):
    slide_id_list = pres.slides._sldIdLst  # pylint: disable=protected-access
    slide_ids = list(slide_id_list)
    if index < 0 or index >= len(slide_ids):
        return
    slide_id_list.remove(slide_ids[index])


def find_title_placeholder(slide):
    for shape in slide.shapes:
        if not shape.is_placeholder:
            continue
        placeholder_type = shape.placeholder_format.type
        if placeholder_type in (PP_PLACEHOLDER.TITLE, PP_PLACEHOLDER.CENTER_TITLE):
            return shape
    if slide.shapes.title:
        return slide.shapes.title
    return None


def collect_body_placeholders(slide, title_shape):
    body_types = []
    for name in ("BODY", "CONTENT", "TEXT", "OBJECT"):
        if hasattr(PP_PLACEHOLDER, name):
            body_types.append(getattr(PP_PLACEHOLDER, name))
    body_types = tuple(body_types)
    placeholders = []
    for shape in slide.shapes:
        if not shape.is_placeholder:
            continue
        if title_shape is not None and shape == title_shape:
            continue
        placeholder_type = shape.placeholder_format.type
        if body_types and placeholder_type in body_types:
            placeholders.append(shape)
    return placeholders


def shape_area(shape):
    try:
        return int(shape.width) * int(shape.height)
    except Exception:
        return 0


def copy_font(src, dest):
    dest.bold = src.bold
    dest.italic = src.italic
    dest.underline = src.underline
    dest.size = src.size
    dest.name = src.name
    try:
        color_type = getattr(src.color, "type", None)
        if color_type == MSO_COLOR_TYPE.RGB:
            dest.color.rgb = src.color.rgb
        elif hasattr(src.color, "theme_color") and src.color.theme_color is not None:
            dest.color.theme_color = src.color.theme_color
        if hasattr(src.color, "brightness") and src.color.brightness is not None:
            dest.color.brightness = src.color.brightness
    except Exception:
        pass


def prune_runs(paragraph, keep=1):
    runs = list(paragraph.runs)
    for run in runs[keep:]:
        run._r.getparent().remove(run._r)


def prune_paragraphs(text_frame, keep=1):
    paragraphs = list(text_frame.paragraphs)
    for para in paragraphs[keep:]:
        para._p.getparent().remove(para._p)


def clear_text(shape):
    if shape is None or not shape.has_text_frame:
        return
    text_frame = shape.text_frame
    if not text_frame.paragraphs:
        return
    paragraph = text_frame.paragraphs[0]
    if paragraph.runs:
        paragraph.runs[0].text = ""
        prune_runs(paragraph, keep=1)
    else:
        paragraph.text = ""
    prune_paragraphs(text_frame, keep=1)


def apply_text(shape, lines):
    if shape is None or not shape.has_text_frame:
        return
    if not lines:
        clear_text(shape)
        return
    text_frame = shape.text_frame
    if not text_frame.paragraphs:
        text_frame.add_paragraph()
    first_paragraph = text_frame.paragraphs[0]
    if first_paragraph.runs:
        first_run = first_paragraph.runs[0]
    else:
        first_run = first_paragraph.add_run()

    first_run.text = lines[0]
    prune_runs(first_paragraph, keep=1)
    prune_paragraphs(text_frame, keep=1)

    for line in lines[1:]:
        paragraph = text_frame.add_paragraph()
        paragraph.level = first_paragraph.level
        run = paragraph.add_run()
        copy_font(first_run.font, run.font)
        run.text = line


def expand_bullets(bullets):
    if len(bullets) != 1:
        return bullets
    text = bullets[0]
    separators = ["。", "；", ";", "、"]
    for sep in separators:
        if sep in text:
            parts = [part.strip() for part in text.split(sep) if part.strip()]
            if len(parts) > 1:
                return parts
    return bullets


def split_bullets(bullets, buckets):
    if buckets <= 1:
        return [bullets]
    bullets = expand_bullets(bullets)
    if len(bullets) <= buckets:
        result = []
        for idx in range(buckets):
            if idx < len(bullets):
                result.append([bullets[idx]])
            else:
                result.append([])
        return result
    base = len(bullets) // buckets
    extra = len(bullets) % buckets
    result = []
    cursor = 0
    for idx in range(buckets):
        size = base + (1 if idx < extra else 0)
        result.append(bullets[cursor:cursor + size])
        cursor += size
    return result


def build_presentation(template_path, output_path, payload):
    pres = Presentation(template_path)

    slides = payload.get("slides", [])
    layout_mode = payload.get("layoutMode", "template")
    layouts = pres.slide_layouts

    existing_count = len(pres.slides)
    target_count = len(slides)

    if existing_count > target_count:
        for index in range(existing_count - 1, target_count - 1, -1):
            delete_slide(pres, index)

    for idx, slide_data in enumerate(slides):
        if idx < len(pres.slides):
            slide = pres.slides[idx]
        else:
            layout_index = 0
            if layout_mode == "sequential" and len(layouts) > 0:
                layout_index = idx % len(layouts)
            elif len(layouts) > 0:
                layout_index = min(1, len(layouts) - 1)
            slide = pres.slides.add_slide(layouts[layout_index])

        title = slide_data.get("title", "")
        bullets = slide_data.get("bullets", []) or []
        if not isinstance(bullets, list):
            bullets = [str(bullets)]
        bullets = [str(item) for item in bullets if str(item).strip()]
        if not bullets and slide_data.get("rawText"):
            raw_text = str(slide_data.get("rawText"))
            bullets = [line.strip() for line in raw_text.splitlines() if line.strip()]

        title_shape = find_title_placeholder(slide)
        if title_shape is not None:
            apply_text(title_shape, [title] if title else [])

        placeholders = collect_body_placeholders(slide, title_shape)
        if not placeholders:
            for shape in slide.shapes:
                if not getattr(shape, "has_text_frame", False):
                    continue
                if title_shape is not None and shape == title_shape:
                    continue
                placeholders.append(shape)

        if placeholders:
            placeholders.sort(key=shape_area, reverse=True)
            use_count = 1
            if len(placeholders) >= 2 and len(bullets) > 5:
                use_count = 2
            used = placeholders[:use_count]
            chunks = split_bullets(bullets, use_count)
            for shape, chunk in zip(used, chunks):
                apply_text(shape, chunk)
            for shape in placeholders[use_count:]:
                if shape.is_placeholder:
                    clear_text(shape)

        notes = slide_data.get("notes", "")
        if notes:
            try:
                slide.notes_slide.notes_text_frame.text = notes
            except Exception:
                pass

    pres.save(output_path)


def main():
    parser = argparse.ArgumentParser(description="Fill PPTX template with slide content.")
    parser.add_argument("--template", required=True, help="Path to PPTX template")
    parser.add_argument("--output", required=True, help="Path to output PPTX")
    parser.add_argument("--data-json", required=True, help="Path to JSON payload")
    args = parser.parse_args()

    template_path = Path(args.template)
    output_path = Path(args.output)
    payload_path = Path(args.data_json)

    if not template_path.exists():
        print(f"Template file not found: {template_path}", file=sys.stderr)
        return 2
    if not zipfile.is_zipfile(template_path):
        print(f"Template file is not a valid .pptx: {template_path}", file=sys.stderr)
        return 4
    if not payload_path.exists():
        print("Payload JSON not found", file=sys.stderr)
        return 3

    with payload_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        build_presentation(str(template_path), str(output_path), payload)
    except Exception as exc:
        print(f"Failed to build presentation: {exc}", file=sys.stderr)
        raise
    return 0


if __name__ == "__main__":
    sys.exit(main())
