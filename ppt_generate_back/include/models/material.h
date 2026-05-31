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
  // FastDFS 存储字段（上传到 FastDFS 后填充）
  std::string fastdfs_file_id;  // 如 group1/M00/00/00/xxx.pdf
  std::string fastdfs_url;      // HTTP 访问 URL
  std::string storage_type = "local";  // local | fastdfs
  std::string rag_status = "unknown";  // unavailable | waiting_extract | extract_failed | not_indexed | indexed
  int rag_chunk_count = 0;
};
