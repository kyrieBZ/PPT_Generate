#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app_config.h"
#include "database/mysql_connection_pool.h"
#include "services/qdrant_client.h"
#include "services/qwen_client.h"

class AiSearchService {
 public:
  struct SearchItem {
    std::uint64_t ppt_id = 0;
    std::string title;
    std::string topic;
    std::string template_name;
    int pages              = 0;
    std::string created_at;
    std::string status;
    std::string output_path;
    double score           = 0.0;
    std::string reason;    // LLM-generated match reason (may be empty)
    bool fallback          = false;
  };

  struct SearchResponse {
    std::vector<SearchItem> results;
    bool fallback = false;  // true when vector DB was unavailable
  };

  AiSearchService(std::shared_ptr<QwenClient> qwen_client,
                  std::shared_ptr<QdrantClient> qdrant_client,
                  std::shared_ptr<MySQLConnectionPool> pool,
                  const AiSearchConfig& cfg);

  // Index a completed PPT request into the vector database.
  // Fetches metadata from MySQL, builds index text, calls embedding API.
  bool IndexPptRequest(std::uint64_t ppt_id, std::uint64_t user_id, std::string& error);

  // Remove a PPT from the vector index (call when PPT is deleted)
  bool RemoveIndex(std::uint64_t ppt_id, std::string& error);

  // Semantic search using natural language query, scoped to user_id.
  // Falls back to SQL LIKE search when Qdrant is unavailable.
  SearchResponse Search(const std::string& query,
                        std::uint64_t user_id,
                        int top_k,
                        bool enable_rerank) const;

  // Rebuild full index for all completed PPTs of a user (async-friendly: returns task count)
  int ReindexUser(std::uint64_t user_id, std::string& error);

  // Rebuild full index for all completed PPTs across all users (admin operation)
  int ReindexAll(std::string& error);

  // Count indexed PPTs for a user
  int GetIndexCount(std::uint64_t user_id, std::string& error);

  bool IsVectorSearchAvailable() const;

 private:
  std::string BuildIndexText(std::uint64_t ppt_id,
                             const std::string& title,
                             const std::string& topic,
                             const std::string& template_name,
                             int pages,
                             const std::string& created_at) const;

  // SQL LIKE fallback search
  std::vector<SearchItem> FallbackSearch(const std::string& query,
                                         std::uint64_t user_id,
                                         int top_k) const;

  std::shared_ptr<QwenClient> qwen_client_;
  std::shared_ptr<QdrantClient> qdrant_client_;
  std::shared_ptr<MySQLConnectionPool> pool_;
  AiSearchConfig cfg_;
};
