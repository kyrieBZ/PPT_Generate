#include "models/ppt_request.h"

#include <algorithm>

namespace {
std::vector<OutlineItem> ReadOutline(const nlohmann::json& data) {
  std::vector<OutlineItem> outline;
  auto it = data.find("outline");
  if (it == data.end() || !it->is_array()) {
    it = data.find("outlineItems");
  }
  if (it == data.end() || !it->is_array()) {
    return outline;
  }
  for (const auto& item : *it) {
    if (!item.is_object()) {
      continue;
    }
    OutlineItem outline_item;
    outline_item.title = item.value("title", "");
    outline_item.summary = item.value("summary", "");
    if (auto kp = item.find("key_points"); kp != item.end() && kp->is_array()) {
      for (const auto& point : *kp) {
        if (point.is_string()) {
          outline_item.key_points.push_back(point.get<std::string>());
        }
      }
    } else if (auto kp = item.find("keyPoints"); kp != item.end() && kp->is_array()) {
      for (const auto& point : *kp) {
        if (point.is_string()) {
          outline_item.key_points.push_back(point.get<std::string>());
        }
      }
    } else if (auto kp = item.find("bullets"); kp != item.end() && kp->is_array()) {
      for (const auto& point : *kp) {
        if (point.is_string()) {
          outline_item.key_points.push_back(point.get<std::string>());
        }
      }
    }
    if (!outline_item.title.empty()) {
      outline.push_back(std::move(outline_item));
    }
  }
  return outline;
}

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
  input.outline = ReadOutline(data);
  input.pages = std::clamp(input.pages, 1, 50);
  return input;
}
