#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Lightweight Qdrant REST client using libcurl.
// Supports: create collection, upsert point, search, delete point.
class QdrantClient {
 public:
  struct SearchResult {
    std::uint64_t ppt_id   = 0;
    std::uint64_t user_id  = 0;
    std::string title;
    std::string topic;
    std::string template_name;
    int pages              = 0;
    std::string created_at;
    std::string status;
    double score           = 0.0;
  };

  QdrantClient(const std::string& host, std::uint16_t port,
               const std::string& collection, int vector_size,
               int timeout_seconds = 5);

  bool IsAvailable() const { return available_; }

  // Ensure collection exists with correct configuration
  bool EnsureCollection(std::string& error);

  // Upsert a single PPT point into the collection
  bool UpsertPoint(std::uint64_t ppt_id,
                   std::uint64_t user_id,
                   const std::string& title,
                   const std::string& topic,
                   const std::string& template_name,
                   int pages,
                   const std::string& created_at,
                   const std::vector<float>& vector,
                   std::string& error);

  // Search for similar vectors, filtered by user_id, returns top_k results
  std::vector<SearchResult> Search(const std::vector<float>& query_vector,
                                   std::uint64_t user_id,
                                   int top_k,
                                   double score_threshold,
                                   std::string& error);

  // Delete a point by ppt_id
  bool DeletePoint(std::uint64_t ppt_id, std::string& error);

  // Count total indexed points for a user
  int CountUserPoints(std::uint64_t user_id, std::string& error);

 private:
  std::string DoRequest(const std::string& method,
                        const std::string& path,
                        const std::string& body,
                        int& http_code);

  std::string base_url_;
  std::string collection_;
  int vector_size_;
  int timeout_seconds_;
  bool available_ = false;
};
