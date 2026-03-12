#!/usr/bin/env node
/**
 * AI Native PPT Builder
 * 接收 DesignSpec JSON，使用 PptxGenJS 低层 API 逐元素渲染
 * Usage: node ai_native_builder.js --data-json <path> --output <path>
 */

"use strict";

const fs   = require("fs");
const path = require("path");

// ---------------------------------------------------------------------------
// 命令行参数解析
// ---------------------------------------------------------------------------
function parseArgs() {
  const args = process.argv.slice(2);
  const result = {};
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--data-json") result.dataJson = args[++i];
    if (args[i] === "--output")    result.output   = args[++i];
  }
  if (!result.dataJson || !result.output) {
    console.error("Usage: node ai_native_builder.js --data-json <path> --output <path>");
    process.exit(1);
  }
  return result;
}

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------

/** 去除 # 前缀，返回 6 位十六进制颜色 */
function hex(color, fallback) {
  if (!color || typeof color !== "string") return fallback || "000000";
  return color.replace(/^#/, "").toUpperCase();
}

/** 将英寸值限制在幻灯片范围内 */
function clamp(val, min, max) {
  return Math.max(min, Math.min(max, val));
}

/** 校验并修正元素坐标，防止越界 */
function validateElement(el, slideW, slideH) {
  const margin = 0;
  el.x = clamp(el.x ?? 0, margin, slideW - 0.1);
  el.y = clamp(el.y ?? 0, margin, slideH - 0.1);
  el.w = clamp(el.w ?? 1, 0.1, slideW - el.x);
  el.h = clamp(el.h ?? 0.5, 0.05, slideH - el.y);
  return el;
}

/** 字体白名单（服务器 Linux 环境下通常可用的 PowerPoint 兼容字体） */
const FONT_WHITELIST = new Set([
  "Arial", "Arial Black", "Arial Narrow", "Calibri", "Calibri Light",
  "Cambria", "Cambria Math", "Candara", "Century Gothic", "Comic Sans MS",
  "Consolas", "Constantia", "Corbel", "Courier New", "Franklin Gothic Medium",
  "Garamond", "Georgia", "Impact", "Lucida Console", "Lucida Sans Unicode",
  "Microsoft Sans Serif", "Palatino Linotype", "Tahoma", "Times New Roman",
  "Trebuchet MS", "Verdana",
]);

function safeFont(font) {
  if (!font) return "Calibri";
  // 精确匹配
  if (FONT_WHITELIST.has(font)) return font;
  // 模糊匹配（忽略大小写）
  const lower = font.toLowerCase();
  for (const f of FONT_WHITELIST) {
    if (f.toLowerCase() === lower) return f;
  }
  return "Calibri";
}

// ---------------------------------------------------------------------------
// 元素渲染
// ---------------------------------------------------------------------------

function renderShape(slide, el, palette) {
  el = validateElement(el, 13.33, 7.5);

  const shapeMap = {
    rect:      "rect",
    roundRect: "roundRect",
    ellipse:   "ellipse",
    triangle:  "triangle",
    line:      "line",
  };
  const shapeName = shapeMap[el.shape] || "rect";

  const opts = {
    x: el.x, y: el.y, w: el.w, h: el.h,
    fill: { color: hex(el.fill || palette.primary) },
  };

  if (el.line && el.line !== "none") {
    opts.line = { color: hex(el.line), width: el.line_width || 1 };
  } else {
    opts.line = { type: "none" };
  }

  if (typeof el.opacity === "number" && el.opacity < 1) {
    opts.transparency = Math.round((1 - el.opacity) * 100);
  }

  if (shapeName === "roundRect" && el.radius) {
    opts.rectRadius = el.radius;
  }

  slide.addShape(shapeName, opts);
}

function renderText(slide, el, typography, palette) {
  el = validateElement(el, 13.33, 7.5);

  const isBold   = el.bold   ?? false;
  const isItalic = el.italic ?? false;
  const fontFace = safeFont(isBold ? typography.title_font : typography.body_font);
  const fontSize = el.font_size || (isBold ? typography.title_size : typography.body_size);
  const color    = hex(el.color || (isBold ? palette.text_light : palette.text_dark));
  const align    = el.align  || "left";
  const valign   = el.valign || "top";

  slide.addText(el.content || "", {
    x: el.x, y: el.y, w: el.w, h: el.h,
    fontSize,
    bold:     isBold,
    italic:   isItalic,
    color,
    align,
    valign,
    fontFace,
    wrap:     true,
    margin:   0,
  });
}

function renderBullets(slide, el, typography, palette) {
  el = validateElement(el, 13.33, 7.5);

  const items = Array.isArray(el.items) ? el.items : [];
  if (items.length === 0) return;

  const fontSize = el.font_size || typography.body_size;
  const color    = hex(el.color || palette.text_dark);
  const fontFace = safeFont(typography.body_font);

  const textItems = items.map((item, idx) => ({
    text: String(item),
    options: {
      bullet:        { type: "bullet" },
      fontSize,
      color,
      fontFace,
      paraSpaceAfter: 4,
      breakLine:      idx < items.length - 1,
    },
  }));

  slide.addText(textItems, {
    x: el.x, y: el.y, w: el.w, h: el.h,
    valign: "top",
    wrap:   true,
  });
}

function renderImage(slide, el) {
  el = validateElement(el, 13.33, 7.5);

  const imgPath = el.path || "";
  if (!imgPath || imgPath.startsWith("__IMAGE_")) {
    // 图片路径未替换，跳过（渲染占位色块）
    slide.addShape("rect", {
      x: el.x, y: el.y, w: el.w, h: el.h,
      fill: { color: "CCCCCC" },
      line: { type: "none" },
    });
    return;
  }

  if (!fs.existsSync(imgPath)) {
    console.warn(`[ai_native_builder] 图片不存在，跳过: ${imgPath}`);
    slide.addShape("rect", {
      x: el.x, y: el.y, w: el.w, h: el.h,
      fill: { color: "CCCCCC" },
      line: { type: "none" },
    });
    return;
  }

  const sizingType = el.sizing || "cover";
  slide.addImage({
    path:   imgPath,
    x:      el.x,
    y:      el.y,
    w:      el.w,
    h:      el.h,
    sizing: { type: sizingType, align: "center", valign: "middle" },
  });
}

/**
 * 渲染图表元素（PptxGenJS addChart）
 * el 格式：
 * {
 *   "type": "chart",
 *   "chart_type": "bar" | "line" | "pie",
 *   "x": 1, "y": 1, "w": 8, "h": 4,
 *   "title": "图表标题（可选）",
 *   "categories": ["A", "B", "C"],
 *   "series": [{ "name": "系列1", "values": [10, 20, 30] }]
 * }
 */
function renderChart(slide, el, palette) {
  el = validateElement(el, 13.33, 7.5);

  const chartTypeMap = {
    bar:  "bar",
    line: "line",
    pie:  "pie",
    // 兼容别名
    column: "bar",
    area:   "area",
  };
  const chartType = chartTypeMap[el.chart_type] || "bar";

  const categories = Array.isArray(el.categories) ? el.categories : ["A", "B", "C"];
  const seriesArr  = Array.isArray(el.series) ? el.series : [{ name: "数据", values: [1, 2, 3] }];

  // PptxGenJS 要求 data 格式：[{ name, labels, values }]
  const data = seriesArr.map((s) => ({
    name:   s.name || "系列",
    labels: categories,
    values: Array.isArray(s.values) ? s.values.map(Number) : categories.map(() => 0),
  }));

  const opts = {
    x: el.x, y: el.y, w: el.w, h: el.h,
    chartColors: [
      hex(palette.accent),
      hex(palette.primary),
      hex(palette.secondary),
      "4472C4", "ED7D31", "A9D18E",
    ],
    showLegend:  data.length > 1,
    showTitle:   !!(el.title),
    title:       el.title || "",
    titleFontSize: 14,
    dataLabelFontSize: 11,
    valAxisMinVal: 0,
  };

  // 饼图特殊处理：只取第一个系列
  if (chartType === "pie") {
    opts.showPercent = true;
    slide.addChart("pie", data.slice(0, 1), opts);
  } else {
    slide.addChart(chartType, data, opts);
  }
}

function renderElement(slide, el, spec) {
  const { palette, typography } = spec.design_spec;

  try {
    switch (el.type) {
      case "shape":   renderShape(slide, el, palette);               break;
      case "text":    renderText(slide, el, typography, palette);    break;
      case "bullets": renderBullets(slide, el, typography, palette); break;
      case "image":   renderImage(slide, el);                        break;
      case "chart":   renderChart(slide, el, palette);               break;
      default:
        console.warn(`[ai_native_builder] 未知元素类型: ${el.type}`);
    }
  } catch (err) {
    console.warn(`[ai_native_builder] 渲染元素失败 (type=${el.type}): ${err.message}`);
  }
}

// ---------------------------------------------------------------------------
// 主流程
// ---------------------------------------------------------------------------
async function main() {
  const args = parseArgs();

  let specRaw;
  try {
    specRaw = fs.readFileSync(args.dataJson, "utf-8");
  } catch (err) {
    console.error(`[ai_native_builder] 读取 DesignSpec 文件失败: ${err.message}`);
    process.exit(1);
  }

  let spec;
  try {
    spec = JSON.parse(specRaw);
  } catch (err) {
    console.error(`[ai_native_builder] DesignSpec JSON 解析失败: ${err.message}`);
    process.exit(1);
  }

  if (!spec.design_spec || !Array.isArray(spec.slides)) {
    console.error("[ai_native_builder] DesignSpec 格式错误：缺少 design_spec 或 slides");
    process.exit(1);
  }

  // 动态加载 pptxgenjs（与现有 pptxgen_builder.js 共享安装）
  let PptxGenJS;
  try {
    PptxGenJS = require("pptxgenjs");
  } catch (err) {
    console.error("[ai_native_builder] 无法加载 pptxgenjs，请先执行 npm install pptxgenjs");
    process.exit(1);
  }

  const pptx = new PptxGenJS();
  const { slide_size, palette, typography } = spec.design_spec;

  // 设置幻灯片尺寸
  const slideW = (slide_size && slide_size.w) ? slide_size.w : 13.33;
  const slideH = (slide_size && slide_size.h) ? slide_size.h : 7.5;
  pptx.defineLayout({ name: "CUSTOM", width: slideW, height: slideH });
  pptx.layout = "CUSTOM";

  // 设置演示文稿元数据
  pptx.author  = "AI Native PPT Generator";
  pptx.company = "PPT Generate System";

  // 按 index 排序幻灯片
  const sortedSlides = [...spec.slides].sort((a, b) => (a.index || 0) - (b.index || 0));

  for (const slideSpec of sortedSlides) {
    const slide = pptx.addSlide();

    // 设置背景色
    const bgColor = slideSpec.background_color || palette.background || "FFFFFF";
    slide.background = { color: hex(bgColor) };

    // 渲染所有元素（按 z-index 顺序：数组顺序即渲染顺序）
    const elements = Array.isArray(slideSpec.elements) ? slideSpec.elements : [];
    for (const el of elements) {
      renderElement(slide, el, spec);
    }

    // 演讲者备注
    if (slideSpec.notes && typeof slideSpec.notes === "string" && slideSpec.notes.trim()) {
      slide.addNotes(slideSpec.notes.trim());
    }
  }

  // 输出文件
  try {
    await pptx.writeFile({ fileName: args.output });
    console.log(`[ai_native_builder] 生成成功: ${args.output}`);
  } catch (err) {
    console.error(`[ai_native_builder] 写入 PPTX 失败: ${err.message}`);
    process.exit(1);
  }
}

main().catch((err) => {
  console.error("[ai_native_builder] 未捕获异常:", err.message);
  process.exit(1);
});
