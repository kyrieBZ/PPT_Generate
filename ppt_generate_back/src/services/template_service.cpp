#include "services/template_service.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace {
std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string ReadString(const nlohmann::json& obj, const std::string& key, const std::string& fallback = "") {
  if (auto it = obj.find(key); it != obj.end() && it->is_string()) {
    return it->get<std::string>();
  }
  return fallback;
}

TemplateTheme ParseTheme(const nlohmann::json& node, const RemoteTemplate& tpl) {
  TemplateTheme theme;
  if (!node.is_object()) {
    theme.primary_color = "#0f172a";
    theme.secondary_color = "#1d4ed8";
    theme.accent_color = "#fbbf24";
    theme.background_image = tpl.preview_image;
    return theme;
  }
  theme.primary_color = ReadString(node, "primary_color", ReadString(node, "primaryColor", "#0f172a"));
  theme.secondary_color = ReadString(node, "secondary_color", ReadString(node, "secondaryColor", "#1d4ed8"));
  theme.accent_color = ReadString(node, "accent_color", ReadString(node, "accentColor", "#f97316"));
  theme.background_image = ReadString(node, "background_image", ReadString(node, "backgroundImage", tpl.preview_image));
  return theme;
}

TemplateLayout ParseLayout(const nlohmann::json& node) {
  TemplateLayout layout;
  if (!node.is_object()) {
    return layout;
  }
  layout.id = ReadString(node, "id", "");
  layout.name = ReadString(node, "name", "");
  layout.type = ReadString(node, "type", "default");
  layout.description = ReadString(node, "description", "");
  layout.accent_color = ReadString(node, "accent_color", ReadString(node, "accentColor", ""));
  layout.background_image = ReadString(node, "background_image", ReadString(node, "backgroundImage", ""));
  if (layout.id.empty()) {
    layout.id = ToLower(layout.type + "_" + layout.name);
  }
  return layout;
}

RemoteTemplate JsonToTemplate(const nlohmann::json& item, const std::filesystem::path& base_dir) {
  RemoteTemplate tpl;
  tpl.id = item.value("id", "");
  tpl.name = item.value("name", "");
  tpl.provider = item.value("provider", "");
  tpl.provider_url = item.value("provider_url", "");
  tpl.description = item.value("description", "");
  tpl.preview_image = item.value("preview_image", item.value("previewImage", ""));
  tpl.download_url = item.value("download_url", item.value("downloadUrl", tpl.provider_url));
  tpl.license = item.value("license", "");
  tpl.prompt = item.value("prompt", "");
  if (item.contains("tags") && item["tags"].is_array()) {
    for (const auto& tag : item["tags"]) {
      if (tag.is_string()) {
        tpl.tags.push_back(tag.get<std::string>());
      }
    }
  }
  if (tpl.id.empty()) {
    tpl.id = ToLower(tpl.name);
  }
  tpl.theme = ParseTheme(item.value("theme", nlohmann::json::object()), tpl);
  if (item.contains("layouts") && item["layouts"].is_array()) {
    for (const auto& layout_node : item["layouts"]) {
      auto layout = ParseLayout(layout_node);
      if (!layout.name.empty()) {
        tpl.layouts.push_back(layout);
      }
    }
  }
  if (tpl.layouts.empty()) {
    tpl.layouts.push_back(TemplateLayout{
        tpl.id + "_default",
        "默认版式",
        "default",
        "标题+要点列表",
        tpl.theme.accent_color,
        tpl.theme.background_image});
  }
  const auto local_file = ReadString(item, "local_file", ReadString(item, "localFile", ""));
  if (!local_file.empty()) {
    std::filesystem::path local_path = local_file;
    if (!local_path.is_absolute()) {
      local_path = (base_dir / local_path).lexically_normal();
    }
    tpl.local_file_path = local_path.string();
    tpl.has_local_file = std::filesystem::exists(local_path);
  }
  return tpl;
}
}

TemplateService::TemplateService(const std::string& catalog_path) {
  catalog_dir_ = std::filesystem::absolute(std::filesystem::path(catalog_path)).parent_path();
  LoadCatalog(catalog_path);
}

void TemplateService::LoadCatalog(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Unable to open template catalog: " + path);
  }

  nlohmann::json data;
  file >> data;
  if (!data.is_array()) {
    throw std::runtime_error("Template catalog must be an array");
  }

  templates_.clear();
  for (const auto& item : data) {
    templates_.push_back(JsonToTemplate(item, catalog_dir_));
  }
}

std::vector<RemoteTemplate> TemplateService::Search(const std::string& query) const {
  if (query.empty()) {
    return templates_;
  }
  const auto lower_query = ToLower(query);
  std::vector<RemoteTemplate> results;
  for (const auto& tpl : templates_) {
    const auto haystack = ToLower(tpl.name + " " + tpl.description + " " + tpl.provider);
    bool match = haystack.find(lower_query) != std::string::npos;
    if (!match) {
      for (const auto& tag : tpl.tags) {
        if (ToLower(tag).find(lower_query) != std::string::npos) {
          match = true;
          break;
        }
      }
    }
    if (match) {
      results.push_back(tpl);
    }
  }
  return results;
}

std::optional<RemoteTemplate> TemplateService::FindById(const std::string& id) const {
  if (id.empty()) {
    return std::nullopt;
  }
  const auto needle = ToLower(id);
  for (const auto& tpl : templates_) {
    if (ToLower(tpl.id) == needle) {
      return tpl;
    }
  }
  return std::nullopt;
}

std::optional<std::string> TemplateService::GetLocalFile(const std::string& id) const {
  if (id.empty()) {
    return std::nullopt;
  }
  const auto needle = ToLower(id);
  for (const auto& tpl : templates_) {
    if (ToLower(tpl.id) == needle && tpl.has_local_file && !tpl.local_file_path.empty()) {
      return tpl.local_file_path;
    }
  }
  return std::nullopt;
}
