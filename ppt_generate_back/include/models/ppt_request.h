#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "models/outline_item.h"

struct PptRequestInput {
  std::string title;
  std::string topic;
  int pages = 10;
  std::string style = "business";
  bool include_images = true;
  bool include_charts = true;
  bool include_notes = false;
  std::string model_id = "qwen-turbo";
  std::string template_id;
  /** "template" = 基于模板, "style" = 基于风格；空则视为 template */
  std::string generate_mode;
  std::vector<OutlineItem> outline;
  bool enable_section_slides = false;
  int section_slide_interval = 4;
  std::string theme_preset;
  std::string material_id;
  /** 链路 3（ai_native）：用户自然语言风格描述，可选 */
  std::string ai_style_prompt;
  /** RAG 知识库增强：是否启用知识库检索注入 */
  bool use_knowledge = false;
  /** RAG 知识库增强：指定参与检索的素材 ID 列表；为空表示检索用户所有已索引素材 */
  std::vector<std::string> rag_material_ids;
  /** F10 风格迁移：参考 PPTX 提取的 StyleSpec JSON 字符串（ai_native 链路注入）*/
  std::string style_spec_json;
  /** F04 图片来源：上传图片的 base64 数组，用于产品图截取等 */
  std::vector<std::string> image_data;
  /** F04 图片来源：图片分析结果 JSON 字符串，用于图表绘制时的真实数据 */
  std::string image_analysis_json;
  /** 图片素材 ID 列表：生成时优先使用用户上传的图片进行配图 */
  std::vector<std::string> image_material_ids;

  static PptRequestInput FromJson(const nlohmann::json& data);
};

struct PptRequest {
  std::uint64_t id = 0;
  std::uint64_t user_id = 0;
  std::string user_name;
  std::string user_email;
  std::string title;
  std::string topic;
  int pages = 0;
  std::string style;
  bool include_images = false;
  bool include_charts = false;
  bool include_notes = false;
  std::string model_id;
  std::string model_name;
  std::string template_id;
  std::string template_name;
  std::string status;
  std::uint64_t created_at = 0;
  std::uint64_t updated_at = 0;
  std::string output_path;
};
