#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "database/mysql_connection_pool.h"
#include "services/qwen_client.h"

class ImageMaterialService {
 public:
  struct ImageMaterial {
    std::string id;
    std::uint64_t user_id = 0;
    std::string filename;
    std::string original_filename;
    std::string storage_path;
    std::string description;
    std::string tags;
    std::string status;
    std::string error_msg;
    std::uint64_t file_size = 0;
    std::uint64_t created_at = 0;
    std::uint64_t updated_at = 0;
  };

  struct SearchResult {
    std::string image_id;
    std::string storage_path;
    std::string filename;
    std::string description;
    double score = 0.0;
  };

  ImageMaterialService(std::shared_ptr<MySQLConnectionPool> pool,
                       std::string upload_dir,
                       std::shared_ptr<QwenClient> qwen_client,
                       std::string qdrant_base_url,
                       int vector_dim = 1024);

  bool IsAvailable() const;
  bool EnsureCollection(std::string& error);

  bool Create(std::uint64_t user_id,
              const std::string& filename,
              const std::string& original_name,
              const std::string& storage_path,
              std::uint64_t file_size,
              ImageMaterial& out,
              std::string& error);

  void AnalyzeAndIndex(const std::string& image_id);

  std::vector<SearchResult> Search(const std::string& query,
                                   std::uint64_t user_id,
                                   const std::vector<std::string>& image_ids = {},
                                   int top_k = 3,
                                   double score_threshold = 0.6) const;

  bool Get(const std::string& image_id,
           std::uint64_t user_id,
           ImageMaterial& out,
           std::string& error) const;

  std::vector<ImageMaterial> List(std::uint64_t user_id, std::string& error) const;

  bool Delete(const std::string& image_id,
              std::uint64_t user_id,
              std::string& error);

  const std::string& upload_dir() const { return upload_dir_; }

 private:
  bool UpdateStatus(const std::string& image_id,
                    const std::string& status,
                    const std::string& description,
                    const std::string& error_msg);

  bool IndexToQdrant(const std::string& image_id,
                     std::uint64_t user_id,
                     const std::string& description,
                     const std::string& filename,
                     const std::string& storage_path,
                     std::string& error);

  bool RemoveFromQdrant(const std::string& image_id, std::string& error);

  std::string DoQdrantRequest(const std::string& method,
                              const std::string& path,
                              const std::string& body,
                              int& http_code) const;

  std::shared_ptr<MySQLConnectionPool> pool_;
  std::string upload_dir_;
  std::shared_ptr<QwenClient> qwen_client_;
  std::string qdrant_base_url_;
  int vector_dim_;

  static constexpr const char* kCollectionName = "user_images";
};
