#include "models/ppt_request.h"

#include <algorithm>

namespace {
bool ReadBool(const nlohmann::json& data, const char* camel, const char* snake, bool fallback) {
  if (const auto it = data.find(camel); it != data.end() && it->is_boolean()) {
    return *it;
  }
  if (const auto it = data.find(snake); it != data.end() && it->is_boolean()) {
    return *it;
  }
  return fallback;
}

std::string ReadString(const nlohmann::json& data, const char* camel, const char* snake) {
  if (const auto it = data.find(camel); it != data.end() && it->is_string()) {
    return it->get<std::string>();
  }
  if (const auto it = data.find(snake); it != data.end() && it->is_string()) {
    return it->get<std::string>();
  }
  return {};
}

int ReadInt(const nlohmann::json& data, const char* camel, const char* snake, int fallback) {
  if (const auto it = data.find(camel); it != data.end() && it->is_number_integer()) {
    return static_cast<int>(*it);
  }
  if (const auto it = data.find(snake); it != data.end() && it->is_number_integer()) {
    return static_cast<int>(*it);
  }
  return fallback;
}
}  // namespace

PptRequestInput PptRequestInput::FromJson(const nlohmann::json& data) {
  PptRequestInput input;
  input.title = ReadString(data, "title", "title");
  input.topic = ReadString(data, "topic", "topic");
  input.pages = ReadInt(data, "pages", "pages", input.pages);
  input.style = ReadString(data, "style", "style");
  if (input.style.empty()) {
    input.style = "business";
  }
  input.include_images = ReadBool(data, "includeImages", "include_images", input.include_images);
  input.include_charts = ReadBool(data, "includeCharts", "include_charts", input.include_charts);
  input.include_notes = ReadBool(data, "includeNotes", "include_notes", input.include_notes);
  const auto model_id = ReadString(data, "modelId", "model_id");
  if (!model_id.empty()) {
    input.model_id = model_id;
  }
  input.template_id = ReadString(data, "templateId", "template_id");
  input.pages = std::clamp(input.pages, 1, 50);
  return input;
}
