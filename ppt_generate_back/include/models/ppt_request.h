#pragma once

#include <cstdint>
#include <string>

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
};

struct PptRequest {
  std::uint64_t id = 0;
  std::uint64_t user_id = 0;
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
  std::string created_at;
  std::string updated_at;
};
