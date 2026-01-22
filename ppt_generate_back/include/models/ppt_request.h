#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

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

  static PptRequestInput FromJson(const nlohmann::json& data);
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
  std::uint64_t created_at = 0;
  std::uint64_t updated_at = 0;
  std::string output_path;
};
