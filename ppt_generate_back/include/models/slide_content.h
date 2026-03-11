#pragma once

#include <optional>
#include <string>
#include <vector>

struct ChartDataItem {
  std::string label;
  double value = 0.0;
};

struct ChartData {
  std::string type;   // "pie" | "bar" | "line" | "doughnut"
  std::string title;  // 图表标题（可选）
  std::vector<ChartDataItem> items;
};

struct SlideContent {
  std::string title;
  std::vector<std::string> bullets;
  std::vector<std::vector<std::string>> bullet_groups;
  std::string raw_text;
  std::vector<std::string> image_prompts;
  std::vector<std::string> image_urls;
  std::vector<std::string> image_paths;
  std::vector<std::string> suggestions;
  std::string layout_hint;
  std::string notes;
  std::optional<ChartData> chart_data;  // 图表数据（可选）
};
