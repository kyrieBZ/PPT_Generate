#pragma once

#include <cstdint>
#include <string>

struct Material {
  std::string id;
  std::uint64_t user_id = 0;
  std::string filename;
  std::string file_type;
  std::string file_path;
  std::uint64_t file_size = 0;
  std::string status;   // pending / extracting / completed / failed
  std::string extract_result;  // JSON string
  std::string error_msg;
  std::string review_result;   // JSON: {"result":"pass|violation|unknown","reason":"...","reviewed_at":unix_ts}
  std::uint64_t created_at = 0;
  std::uint64_t updated_at = 0;
};
