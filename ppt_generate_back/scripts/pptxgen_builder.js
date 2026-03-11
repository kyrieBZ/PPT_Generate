#!/usr/bin/env node
/**
 * PptxGenJS-based PPT builder (scheme B). P3: image style + spacing.
 * Usage: node pptxgen_builder.js --data-json <path> --output <path>
 */

const fs = require("fs");
const path = require("path");

// P3: 间距与留白（skill 3.7），全篇一致
const MARGIN_X = 0.5;
const GAP_ACCENT_CONTENT = 0.2;
const GAP_TITLE_BODY = 0.35;
const GAP_TEXT_IMAGE = 0.4;

// P2: 主题预设（与 pptx skill 配色表一致）
const THEME_PRESETS = {
  midnight: { primary: "1E2761", secondary: "CADCFC", accent: "FFFFFF" },
  forest: { primary: "2C5F2D", secondary: "97BC62", accent: "F5F5F5" },
  charcoal: { primary: "36454F", secondary: "F2F2F2", accent: "212121" },
  coral: { primary: "F96167", secondary: "F9E795", accent: "2F3C7E" },
  teal: { primary: "028090", secondary: "00A896", accent: "02C39A" },
  ocean: { primary: "065A82", secondary: "1C7293", accent: "21295C" },
  berry: { primary: "6D2E46", secondary: "A26769", accent: "ECE2D0" },
  sage: { primary: "84B59F", secondary: "69A297", accent: "50808E" },
  terracotta: { primary: "B85042", secondary: "E7E8D1", accent: "A7BEAE" },
  cherry: { primary: "990011", secondary: "FCF6F5", accent: "2F3C7E" },
};

// P2: 字体搭配（skill 推荐），按 theme 轮换
const FONT_PAIRS = [
  { title: "Georgia", body: "Calibri" },
  { title: "Cambria", body: "Calibri" },
  { title: "Trebuchet MS", body: "Calibri" },
];

// P2: 字号与 skill 一致（pt）
const FONT_SIZES = {
  coverTitle: 44,
  coverSubtitle: 24,
  sectionNumber: 72,
  sectionTitle: 36,
  sectionSubtitle: 18,
  closingTitle: 38,
  closingLine: 22,
  contentTitle: 36,
  contentTitleSmall: 30,
  body: 14,
  bodySmall: 13,
  quote: 22,
  caption: 11,
};

function parseArgs() {
  const args = process.argv.slice(2);
  let dataJson = null;
  let output = null;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--data-json" && args[i + 1]) dataJson = args[++i];
    else if (args[i] === "--output" && args[i + 1]) output = args[++i];
  }
  if (!dataJson || !output) {
    console.error("Usage: node pptxgen_builder.js --data-json <path> --output <path>");
    process.exit(2);
  }
  return { dataJson, output };
}

function hexNoHash(hex) {
  if (!hex || typeof hex !== "string") return "1E2761";
  const s = String(hex).replace(/^#/, "").trim();
  return s.length >= 6 ? s.slice(0, 6) : "1E2761";
}

const PRESET_ORDER = Object.keys(THEME_PRESETS);

function getTheme(payload) {
  const presetName = (payload.themePreset || "").trim().toLowerCase();
  if (presetName && THEME_PRESETS[presetName]) {
    return { ...THEME_PRESETS[presetName] };
  }
  const t = payload.theme || {};
  return {
    primary: hexNoHash(t.primaryColor || t.primary),
    secondary: hexNoHash(t.secondaryColor || t.secondary),
    accent: hexNoHash(t.accentColor || t.accent),
  };
}

/** P2: 按 themePreset 轮换字体对，保证同一请求内一致 */
function getTypography(payload) {
  const presetName = (payload.themePreset || "").trim().toLowerCase();
  const idx = PRESET_ORDER.indexOf(presetName);
  const pair = FONT_PAIRS[idx >= 0 ? idx % FONT_PAIRS.length : 0];
  return { titleFont: pair.title, bodyFont: pair.body };
}

function flattenBullets(slide) {
  const groups = slide.bulletGroups || slide.bullet_groups;
  if (Array.isArray(groups) && groups.length > 0) {
    return groups.flat().filter(Boolean).map(String);
  }
  const bullets = slide.bullets || [];
  return Array.isArray(bullets) ? bullets.map(String) : [String(bullets || "")];
}

function getImageSource(slide, baseDir) {
  const paths = slide.imagePaths || slide.image_paths || [];
  const urls = slide.imageUrls || slide.image_urls || [];
  const firstPath = paths.find((p) => p && fs.existsSync(path.isAbsolute(p) ? p : path.join(baseDir, p)));
  if (firstPath) {
    return { path: path.isAbsolute(firstPath) ? firstPath : path.join(baseDir, firstPath) };
  }
  if (urls.length > 0 && urls[0]) return { path: urls[0] };
  return null;
}

const LAYOUT_TYPES = ["cover", "section", "closing", "content_text_image", "content_text_only", "content_bullets_heavy", "content_quote", "content_chart"];
const QUOTE_MAX_LEN = 80;

function getLayoutType(slideData, index, total, dataDir) {
  const hint = (slideData.layoutHint || slideData.layout_hint || "").trim().toLowerCase();
  if (hint && LAYOUT_TYPES.includes(hint)) return hint;
  if (index === 0) return "cover";
  if (index === total - 1) return "closing";
  // 有图表数据时优先使用图表布局
  if (isValidChartData(slideData.chartData || slideData.chart_data)) return "content_chart";
  const bullets = flattenBullets(slideData);
  const hasImage = !!getImageSource(slideData, dataDir);
  if (hasImage && bullets.length <= 4) return "content_text_image";
  if (bullets.length === 1) {
    const text = String(bullets[0]).trim();
    if (text.length > 0 && text.length <= QUOTE_MAX_LEN) return "content_quote";
  }
  if (bullets.length >= 6) return "content_bullets_heavy";
  return "content_text_only";
}

/** 验证图表数据是否合法（至少2个数据项，每项有 label 和数字 value） */
function isValidChartData(cd) {
  if (!cd || typeof cd !== "object") return false;
  if (!Array.isArray(cd.items) || cd.items.length < 2) return false;
  return cd.items.every((item) => item && item.label && typeof item.value === "number");
}

/** 根据主题色生成图表配色板（6色） */
function generateChartColors(theme) {
  // 以 primary、secondary、accent 为基础，派生出 6 个颜色
  const base = [theme.primary, theme.secondary, theme.accent];
  const lighten = (hex, amount) => {
    const num = parseInt(hex.replace(/^#/, ""), 16);
    const r = Math.min(255, ((num >> 16) & 0xff) + amount);
    const g = Math.min(255, ((num >> 8) & 0xff) + amount);
    const b = Math.min(255, (num & 0xff) + amount);
    return ((r << 16) | (g << 8) | b).toString(16).padStart(6, "0").toUpperCase();
  };
  return [
    base[0],
    base[1],
    base[2],
    lighten(base[0], 40),
    lighten(base[1], 40),
    lighten(base[2], 40),
  ];
}

/** 构建图表幻灯片（content_chart 布局） */
function buildChartSlide(slide, pres, slideData, theme, typo, sz) {
  const cd = slideData.chartData || slideData.chart_data;
  const CHART_TYPE_MAP = {
    pie: pres.charts.PIE,
    bar: pres.charts.BAR,
    line: pres.charts.LINE,
    doughnut: pres.charts.DOUGHNUT,
  };
  const chartType = CHART_TYPE_MAP[(cd.type || "bar").toLowerCase()] || pres.charts.BAR;
  const dataForChart = [
    {
      name: cd.title || slideData.title || "",
      labels: cd.items.map((i) => String(i.label)),
      values: cd.items.map((i) => Number(i.value)),
    },
  ];
  const chartColors = generateChartColors(theme);
  const title = String(slideData.title || "").trim() || " ";
  const bullets = flattenBullets(slideData);
  const hasBullets = bullets.length > 0;

  addContentSlideBase(slide, pres, theme, MARGIN_X);

  if (hasBullets) {
    // 左文右图布局
    const textW = 4.3;
    const chartX = MARGIN_X + 0.06 + GAP_ACCENT_CONTENT + textW + GAP_TEXT_IMAGE;
    const chartW = 10 - chartX - MARGIN_X;
    const chartH = 3.8;
    const chartY = 1.0;

    slide.addText(title, {
      x: MARGIN_X + 0.06 + GAP_ACCENT_CONTENT,
      y: 0.4,
      w: textW,
      h: 0.65,
      fontSize: sz.contentTitleSmall,
      bold: true,
      color: theme.primary,
      fontFace: typo.titleFont,
      margin: 0,
    });
    const runs = bullets.map((b, idx) => ({
      text: String(b).trim() || " ",
      options: {
        bullet: true,
        breakLine: idx < bullets.length - 1,
        fontSize: sz.body,
        color: "334155",
        fontFace: typo.bodyFont,
      },
    }));
    slide.addText(runs, {
      x: MARGIN_X + 0.06 + GAP_ACCENT_CONTENT,
      y: 1.15,
      w: textW,
      h: 4.0,
      fontSize: sz.body,
      color: "334155",
      fontFace: typo.bodyFont,
      valign: "top",
      margin: 0,
    });
    slide.addChart(chartType, dataForChart, {
      x: chartX,
      y: chartY,
      w: chartW,
      h: chartH,
      showLegend: true,
      legendPos: "b",
      legendFontSize: 9,
      showTitle: !!(cd.title),
      title: cd.title || "",
      titleFontSize: 11,
      chartColors: chartColors,
      dataLabelFontSize: 9,
      showValue: chartType !== pres.charts.PIE && chartType !== pres.charts.DOUGHNUT,
      showPercent: chartType === pres.charts.PIE || chartType === pres.charts.DOUGHNUT,
    });
  } else {
    // 全幅图表布局：标题在顶，图表占主体
    slide.addText(title, {
      x: MARGIN_X + 0.06 + GAP_ACCENT_CONTENT,
      y: 0.4,
      w: 10 - MARGIN_X - 0.06 - GAP_ACCENT_CONTENT - MARGIN_X,
      h: 0.65,
      fontSize: sz.contentTitle,
      bold: true,
      color: theme.primary,
      fontFace: typo.titleFont,
      margin: 0,
    });
    slide.addChart(chartType, dataForChart, {
      x: MARGIN_X + 0.06 + GAP_ACCENT_CONTENT,
      y: 1.15,
      w: 10 - MARGIN_X - 0.06 - GAP_ACCENT_CONTENT - MARGIN_X,
      h: 4.1,
      showLegend: true,
      legendPos: "b",
      legendFontSize: 10,
      showTitle: !!(cd.title),
      title: cd.title || "",
      titleFontSize: 12,
      chartColors: chartColors,
      dataLabelFontSize: 10,
      showValue: chartType !== pres.charts.PIE && chartType !== pres.charts.DOUGHNUT,
      showPercent: chartType === pres.charts.PIE || chartType === pres.charts.DOUGHNUT,
    });
  }
}

function addContentSlideBase(slide, pres, theme, marginX, contentX, contentW) {
  slide.background = { color: "F8FAFC" };
  const accentW = 0.06;
  slide.addShape(pres.shapes.RECTANGLE, {
    x: marginX,
    y: 0,
    w: accentW,
    h: 5.625,
    fill: { color: theme.accent },
    line: { type: "none" },
  });
  const cx = contentX ?? marginX + accentW + GAP_ACCENT_CONTENT;
  const cw = contentW ?? 10 - marginX - accentW - GAP_ACCENT_CONTENT - marginX;
  return { contentX: cx, contentW: cw };
}

function buildPresentation(payload, outputPath) {
  const PptxGenJS = require("pptxgenjs");
  const pres = new PptxGenJS();
  pres.layout = "LAYOUT_16x9";
  pres.author = "PPT Generate";
  pres.title = "Presentation";

  const theme = getTheme(payload);
  const typo = getTypography(payload);
  const slides = payload.slides || [];
  const dataDir = path.dirname(outputPath);
  const total = slides.length;
  const sz = FONT_SIZES;

  for (let i = 0; i < total; i++) {
    const slideData = slides[i];
    const title = String(slideData.title || "").trim() || " ";
    const bullets = flattenBullets(slideData);
    const layoutType = getLayoutType(slideData, i, total, dataDir);
    const slide = pres.addSlide();

    // ——— P1: 封面 ——— 0.5" 边距，深色背景，底部 accent 条与总结页呼应
    if (layoutType === "cover") {
      slide.background = { color: theme.primary };
      const contentW = 10 - MARGIN_X * 2;
      const centerY = 2.6;
      slide.addText(title, {
        x: MARGIN_X, y: centerY - 0.65, w: contentW, h: 1.3,
        fontSize: sz.coverTitle, bold: true, color: "FFFFFF", fontFace: typo.titleFont,
        align: "center", valign: "middle", margin: 0,
      });
      if (bullets.length > 0) {
        slide.addText(bullets[0], {
          x: MARGIN_X, y: centerY + 0.75, w: contentW, h: 0.75,
          fontSize: sz.coverSubtitle, color: "E2E8F0", fontFace: typo.bodyFont, align: "center", margin: 0,
        });
      }
      slide.addShape(pres.shapes.RECTANGLE, {
        x: 0, y: 5.2, w: 10, h: 0.425,
        fill: { color: theme.accent }, line: { type: "none" },
      });
      continue;
    }

    // ——— P1: 章节页 ——— 左侧竖条与内容页统一
    if (layoutType === "section") {
      slide.background = { color: theme.primary };
      const accentW = 0.06;
      const contentX = MARGIN_X + accentW + GAP_ACCENT_CONTENT;
      const contentW = 10 - contentX - MARGIN_X;
      slide.addShape(pres.shapes.RECTANGLE, {
        x: MARGIN_X, y: 0, w: accentW, h: 5.625,
        fill: { color: theme.accent }, line: { type: "none" },
      });
      const sectionNum = (title.match(/(\d+)/) || [])[1] || String(i + 1);
      slide.addText(sectionNum, {
        x: contentX, y: 0.9, w: contentW, h: 1.5,
        fontSize: sz.sectionNumber, bold: true, color: "FFFFFF", fontFace: typo.titleFont,
        align: "center", valign: "middle", margin: 0,
      });
      slide.addText(title, {
        x: contentX, y: 2.6, w: contentW, h: 0.9,
        fontSize: sz.sectionTitle, bold: true, color: "FFFFFF", fontFace: typo.titleFont,
        align: "center", valign: "middle", margin: 0,
      });
      if (bullets.length > 0) {
        slide.addText(bullets[0], {
          x: contentX, y: 3.6, w: contentW, h: 0.55,
          fontSize: sz.sectionSubtitle, color: "CBD5E1", fontFace: typo.bodyFont, align: "center", margin: 0,
        });
      }
      continue;
    }

    // ——— P1: 总结页 ——— 与封面三明治呼应，无内容时默认「谢谢」，底部 accent 条
    if (layoutType === "closing") {
      slide.background = { color: theme.primary };
      const contentW = 10 - MARGIN_X * 2;
      const blockTop = 2.5;
      slide.addText(title, {
        x: MARGIN_X, y: blockTop, w: contentW, h: 0.95,
        fontSize: sz.closingTitle, bold: true, color: "FFFFFF", fontFace: typo.titleFont,
        align: "center", valign: "middle", margin: 0,
      });
      const closingLine = bullets.length > 0 ? bullets[0] : "谢谢";
      slide.addText(closingLine, {
        x: MARGIN_X, y: blockTop + 1.1, w: contentW, h: 0.7,
        fontSize: sz.closingLine, color: "E2E8F0", fontFace: typo.bodyFont, align: "center", margin: 0,
      });
      slide.addShape(pres.shapes.RECTANGLE, {
        x: 0, y: 5.2, w: 10, h: 0.425,
        fill: { color: theme.accent }, line: { type: "none" },
      });
      continue;
    }

    const accentW = 0.06;
    const contentX = MARGIN_X + accentW + GAP_ACCENT_CONTENT;
    const contentW = 10 - contentX - MARGIN_X;
    const imgSrc = getImageSource(slideData, dataDir);

    if (layoutType === "content_chart") {
      buildChartSlide(slide, pres, slideData, theme, typo, sz);
      continue;
    }

    if (layoutType === "content_quote") {
      slide.background = { color: "F8FAFC" };
      slide.addShape(pres.shapes.RECTANGLE, {
        x: MARGIN_X, y: 0, w: accentW, h: 5.625,
        fill: { color: theme.accent }, line: { type: "none" },
      });
      slide.addText(title, {
        x: contentX, y: 0.4, w: contentW, h: 0.6,
        fontSize: sz.contentTitleSmall, bold: true, color: theme.primary, fontFace: typo.titleFont, margin: 0,
      });
      const quote = bullets[0] ? String(bullets[0]).trim() : " ";
      slide.addShape(pres.shapes.RECTANGLE, {
        x: contentX, y: 1.3, w: contentW, h: 2.2,
        fill: { color: theme.secondary, transparency: 85 }, line: { type: "none" },
      });
      slide.addText(quote, {
        x: contentX + 0.2, y: 1.5, w: contentW - 0.4, h: 1.8,
        fontSize: sz.quote, italic: true, color: theme.primary, fontFace: typo.titleFont,
        align: "left", valign: "middle", margin: 0,
      });
      continue;
    }

    if (layoutType === "content_text_image" && imgSrc) {
      const imageOnRight = i % 2 === 0;
      const imgW = 3.8;
      const bodyW = contentW - imgW - GAP_TEXT_IMAGE;
      addContentSlideBase(slide, pres, theme, MARGIN_X);
      const imgX = imageOnRight ? contentX + bodyW + GAP_TEXT_IMAGE : contentX;
      const bodyX = imageOnRight ? contentX : contentX + imgW + GAP_TEXT_IMAGE;
      slide.addText(title, {
        x: bodyX, y: 0.4, w: bodyW, h: 0.65,
        fontSize: sz.contentTitle, bold: true, color: theme.primary, fontFace: typo.titleFont, margin: 0,
      });
      if (bullets.length > 0) {
        const runs = bullets.map((b, idx) => ({
          text: String(b).trim() || " ",
          options: { bullet: true, breakLine: idx < bullets.length - 1, fontSize: sz.body, color: "334155", fontFace: typo.bodyFont },
        }));
        slide.addText(runs, {
          x: bodyX, y: 1.15, w: bodyW, h: 4,
          fontSize: sz.body, color: "334155", fontFace: typo.bodyFont, valign: "top", margin: 0,
        });
      }
      try {
        slide.addImage({
          path: imgSrc.path,
          x: imgX, y: 1.05, w: imgW, h: 3.6,
          sizing: { type: "contain", w: imgW, h: 3.6 },
          rounding: true,
          shadow: { type: "outer", blur: 4, offset: 2, angle: 135, color: "000000", opacity: 0.12 },
        });
      } catch (e) {
        console.error("Image add failed:", e.message);
      }
      continue;
    }

    if (layoutType === "content_bullets_heavy") {
      addContentSlideBase(slide, pres, theme, MARGIN_X);
      slide.addText(title, {
        x: contentX, y: 0.4, w: contentW, h: 0.6,
        fontSize: sz.contentTitleSmall, bold: true, color: theme.primary, fontFace: typo.titleFont, margin: 0,
      });
      const colGap = GAP_TITLE_BODY;
      const colW = (contentW - colGap) / 2;
      const half = Math.ceil(bullets.length / 2);
      for (let col = 0; col < 2; col++) {
        const start = col === 0 ? 0 : half;
        const end = col === 0 ? half : bullets.length;
        const slice = bullets.slice(start, end);
        const runs = slice.map((b, idx) => ({
          text: String(b).trim() || " ",
          options: { bullet: true, breakLine: idx < slice.length - 1, fontSize: sz.bodySmall, color: "334155", fontFace: typo.bodyFont },
        }));
        slide.addText(runs, {
          x: contentX + col * (colW + colGap), y: 1.1, w: colW, h: 4,
          fontSize: sz.bodySmall, color: "334155", fontFace: typo.bodyFont, valign: "top", margin: 0,
        });
      }
      slide.addShape(pres.shapes.RECTANGLE, {
        x: contentX + colW + colGap / 2 - 0.01, y: 1, w: 0.02, h: 4.2,
        fill: { color: theme.secondary, transparency: 60 }, line: { type: "none" },
      });
      continue;
    }

    // content_text_only (default content)
    addContentSlideBase(slide, pres, theme, MARGIN_X);
    slide.addText(title, {
      x: contentX, y: 0.4, w: contentW, h: 0.7,
      fontSize: sz.contentTitle, bold: true, color: theme.primary, fontFace: typo.titleFont, margin: 0,
    });
    const bodyTop = 0.4 + 0.7 + GAP_TITLE_BODY;
    slide.addShape(pres.shapes.RECTANGLE, {
      x: contentX, y: bodyTop - 0.05, w: contentW, h: 4.0,
      fill: { color: theme.secondary, transparency: 92 }, line: { type: "none" },
    });
    if (bullets.length > 0) {
      const runs = bullets.map((b, idx) => ({
        text: String(b).trim() || " ",
        options: { bullet: true, breakLine: idx < bullets.length - 1, fontSize: sz.body, color: "334155", fontFace: typo.bodyFont },
      }));
      slide.addText(runs, {
        x: contentX + GAP_ACCENT_CONTENT, y: bodyTop, w: contentW - GAP_ACCENT_CONTENT * 2, h: 3.7,
        fontSize: sz.body, color: "334155", fontFace: typo.bodyFont, valign: "top", margin: 0,
      });
    }
    if (imgSrc) {
      try {
        const imgW = 3;
        const imgH = 2.8;
        slide.addImage({
          path: imgSrc.path,
          x: contentX + contentW - imgW - GAP_TEXT_IMAGE, y: 1.2, w: imgW, h: imgH,
          sizing: { type: "contain", w: imgW, h: imgH },
          rounding: true,
          shadow: { type: "outer", blur: 4, offset: 2, angle: 135, color: "000000", opacity: 0.12 },
        });
      } catch (e) {
        console.error("Image add failed:", e.message);
      }
    }
  }

  pres.writeFile({ fileName: outputPath }).then(() => process.exit(0)).catch((err) => {
    console.error("PptxGenJS write failed:", err);
    process.exit(1);
  });
}

function main() {
  const { dataJson, output } = parseArgs();
  if (!fs.existsSync(dataJson)) {
    console.error("Data JSON not found:", dataJson);
    process.exit(3);
  }
  let payload;
  try {
    payload = JSON.parse(fs.readFileSync(dataJson, "utf8"));
  } catch (e) {
    console.error("Invalid JSON:", e.message);
    process.exit(4);
  }
  const outDir = path.dirname(output);
  if (outDir && !fs.existsSync(outDir)) {
    fs.mkdirSync(outDir, { recursive: true });
  }
  buildPresentation(payload, path.resolve(output));
}

main();
