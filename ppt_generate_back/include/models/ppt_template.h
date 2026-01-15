#pragma once

#include <string>
#include <vector>

struct TemplateLayout {
  std::string id;
  std::string name;
  std::string type;
  std::string description;
  std::string accent_color;
  std::string background_image;
};

struct TemplateTheme {
  std::string primary_color;
  std::string secondary_color;
  std::string accent_color;
  std::string background_image;
};

struct RemoteTemplate {
  std::string id;
  std::string name;
  std::string provider;
  std::string provider_url;
  std::string description;
  std::string preview_image;
  std::string download_url;
  std::string license;
  std::vector<std::string> tags;
  TemplateTheme theme;
  std::vector<TemplateLayout> layouts;
  std::string local_file_path;
  bool has_local_file = false;
};
