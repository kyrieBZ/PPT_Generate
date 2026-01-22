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

std::string NormalizeKey(std::string value) {
  std::string result;
  result.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch)) {
      result.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return result;
}

bool LooksLikePptx(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }
  char signature[2] = {0};
  input.read(signature, 2);
  if (input.gcount() != 2 || signature[0] != 'P' || signature[1] != 'K') {
    return false;
  }
  input.seekg(0, std::ios::end);
  auto size = input.tellg();
  if (size <= 0) {
    return false;
  }
  constexpr std::streamoff kMaxSearch = 65536 + 22;
  const auto read_size = std::min<std::streamoff>(size, kMaxSearch);
  input.seekg(size - read_size);
  std::string buffer(static_cast<size_t>(read_size), '\0');
  input.read(buffer.data(), read_size);
  const std::string eocd = std::string("\x50\x4b\x05\x06", 4);
  return buffer.find(eocd) != std::string::npos;
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

void RefreshLocalFile(RemoteTemplate& tpl) {
  if (!tpl.local_file_path.empty()) {
    tpl.has_local_file = std::filesystem::exists(tpl.local_file_path) &&
                         LooksLikePptx(tpl.local_file_path);
  } else {
    tpl.has_local_file = false;
  }
}

std::optional<std::string> ResolveTemplateFallback(const RemoteTemplate& tpl,
                                                   const std::filesystem::path& catalog_dir) {
  const auto templates_dir = (catalog_dir / "../assets/templates").lexically_normal();
  if (!std::filesystem::exists(templates_dir)) {
    return std::nullopt;
  }
  const auto id_key = NormalizeKey(tpl.id);
  const auto name_key = NormalizeKey(tpl.name);
  for (const auto& entry : std::filesystem::directory_iterator(templates_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto path = entry.path();
    if (path.extension() != ".pptx") {
      continue;
    }
    if (!LooksLikePptx(path)) {
      continue;
    }
    const auto filename_key = NormalizeKey(path.stem().string());
    if ((!id_key.empty() && filename_key.find(id_key) != std::string::npos) ||
        (!name_key.empty() && filename_key.find(name_key) != std::string::npos)) {
      return path.string();
    }
  }
  return std::nullopt;
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
      auto copy = tpl;
      RefreshLocalFile(copy);
      results.push_back(std::move(copy));
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
      auto copy = tpl;
      RefreshLocalFile(copy);
      return copy;
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
    if (ToLower(tpl.id) == needle && !tpl.local_file_path.empty()) {
      if (std::filesystem::exists(tpl.local_file_path) && LooksLikePptx(tpl.local_file_path)) {
        return tpl.local_file_path;
      }
      if (auto fallback = ResolveTemplateFallback(tpl, catalog_dir_)) {
        return fallback;
      }
      return std::nullopt;
    }
    if (ToLower(tpl.id) == needle) {
      if (auto fallback = ResolveTemplateFallback(tpl, catalog_dir_)) {
        return fallback;
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}
