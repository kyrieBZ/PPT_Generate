#include "services/ai_search_service.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "logger.h"

AiSearchService::AiSearchService(std::shared_ptr<QwenClient> qwen_client,
                                 std::shared_ptr<QdrantClient> qdrant_client,
                                 std::shared_ptr<MySQLConnectionPool> pool,
                                 const AiSearchConfig& cfg)
    : qwen_client_(std::move(qwen_client)),
      qdrant_client_(std::move(qdrant_client)),
      pool_(std::move(pool)),
      cfg_(cfg) {
  if (qdrant_client_ && qdrant_client_->IsAvailable()) {
    std::string err;
    if (!qdrant_client_->EnsureCollection(err)) {
      Logger::Warn("AiSearchService: EnsureCollection failed: " + err);
    } else {
      Logger::Info("AiSearchService: vector search ready (collection=" +
                   cfg_.collection_name + ")");
    }
  }
}

bool AiSearchService::IsVectorSearchAvailable() const {
  return cfg_.enabled && qdrant_client_ && qdrant_client_->IsAvailable() &&
         qwen_client_ && qwen_client_->IsEnabled();
}

std::string AiSearchService::BuildIndexText(std::uint64_t ppt_id,
                                            const std::string& title,
                                            const std::string& topic,
                                            const std::string& template_name,
                                            int pages,
                                            const std::string& created_at) const {
  std::ostringstream oss;
  oss << "标题：" << title << "\n";
  oss << "主题描述：" << topic << "\n";
  if (!template_name.empty()) {
    oss << "模板风格：" << template_name << "\n";
  }
  oss << "幻灯片数量：" << pages << "页\n";
  if (!created_at.empty()) {
    oss << "生成时间：" << created_at << "\n";
  }

  // Try to enrich with slide content from structure JSON
  // Convention: assets/generated/<ppt_id>_structure.json or <ppt_id>.json.preview
  const std::vector<std::string> candidate_paths = {
    "assets/generated/" + std::to_string(ppt_id) + "_structure.json",
    "assets/generated/" + std::to_string(ppt_id) + ".pptx.preview.json",
    "assets/generated/" + std::to_string(ppt_id) + ".preview.json",
  };

  for (const auto& path : candidate_paths) {
    std::ifstream f(path);
    if (!f.is_open()) continue;
    try {
      nlohmann::json structure = nlohmann::json::parse(f);
      const auto& slides = structure.is_array() ? structure
                         : (structure.contains("slides") ? structure.at("slides")
                                                         : nlohmann::json::array());
      if (!slides.is_array()) break;
      oss << "---\n幻灯片内容摘要：\n";
      int slide_num = 0;
      for (const auto& slide : slides) {
        if (++slide_num > 10) break;
        std::string s_title = slide.value("title", "");
        std::string s_body;
        if (slide.contains("content") && slide["content"].is_string()) {
          s_body = slide["content"].get<std::string>().substr(0, 60);
        } else if (slide.contains("bullets") && slide["bullets"].is_array()) {
          for (const auto& b : slide["bullets"]) {
            if (b.is_string()) { s_body += b.get<std::string>() + " "; }
          }
          if (s_body.size() > 60) s_body = s_body.substr(0, 60);
        }
        oss << "第" << slide_num << "页：" << s_title;
        if (!s_body.empty()) oss << " - " << s_body;
        oss << "\n";
      }
      break;
    } catch (...) {
      break;
    }
  }

  return oss.str();
}

bool AiSearchService::IndexPptRequest(std::uint64_t ppt_id,
                                      std::uint64_t user_id,
                                      std::string& error) {
  if (!IsVectorSearchAvailable()) {
    error = "Vector search not available";
    return false;
  }

  // Fetch metadata from MySQL
  auto scoped_conn = pool_->GetConnection();
  MYSQL* conn = scoped_conn.Get();
  if (!conn) {
    error = "No DB connection";
    return false;
  }

  std::string title, topic, template_name, created_at;
  int pages = 0;

  const std::string sql =
      "SELECT title, topic, template_name, pages, created_at "
      "FROM ppt_requests WHERE id=" + std::to_string(ppt_id) +
      " AND user_id=" + std::to_string(user_id) +
      " AND status='completed' LIMIT 1";

  MYSQL_RES* res = nullptr;
  if (mysql_query(conn, sql.c_str()) == 0) {
    res = mysql_store_result(conn);
  }

  if (!res) {
    error = "PPT record not found or not completed";
    return false;
  }

  MYSQL_ROW row = mysql_fetch_row(res);
  if (row) {
    title         = row[0] ? row[0] : "";
    topic         = row[1] ? row[1] : "";
    template_name = row[2] ? row[2] : "";
    pages         = row[3] ? std::stoi(row[3]) : 0;
    created_at    = row[4] ? row[4] : "";
  }
  mysql_free_result(res);

  if (title.empty() && topic.empty()) {
    error = "PPT record has no content to index";
    return false;
  }

  const std::string index_text = BuildIndexText(ppt_id, title, topic,
                                                template_name, pages, created_at);
  const std::vector<float> embedding =
      qwen_client_->GetEmbedding(index_text, cfg_.embedding_model);

  if (embedding.empty()) {
    error = "Embedding API returned empty vector";
    return false;
  }

  if (!qdrant_client_->UpsertPoint(ppt_id, user_id, title, topic, template_name,
                                    pages, created_at, embedding, error)) {
    return false;
  }

  Logger::Info("AiSearchService: indexed ppt_id=" + std::to_string(ppt_id));
  return true;
}

bool AiSearchService::RemoveIndex(std::uint64_t ppt_id, std::string& error) {
  if (!qdrant_client_ || !qdrant_client_->IsAvailable()) {
    return true;  // Not available, nothing to remove
  }
  return qdrant_client_->DeletePoint(ppt_id, error);
}

AiSearchService::SearchResponse AiSearchService::Search(const std::string& query,
                                                         std::uint64_t user_id,
                                                         int top_k,
                                                         bool enable_rerank) const {
  SearchResponse response;

  if (!IsVectorSearchAvailable()) {
    response.fallback = true;
    response.results = FallbackSearch(query, user_id, top_k);
    return response;
  }

  // Step 1: Embed the query
  const std::vector<float> query_vec =
      qwen_client_->GetEmbedding(query, cfg_.embedding_model);

  if (query_vec.empty()) {
    Logger::Warn("AiSearchService: query embedding failed, falling back to SQL search");
    response.fallback = true;
    response.results = FallbackSearch(query, user_id, top_k);
    return response;
  }

  // Step 2: ANN search in Qdrant
  std::string search_error;
  const int retrieve_k = std::max(top_k, cfg_.top_k_retrieve);
  auto qdrant_results = qdrant_client_->Search(
      query_vec, user_id, retrieve_k, cfg_.score_threshold, search_error);

  if (!search_error.empty()) {
    Logger::Warn("AiSearchService: Qdrant search error: " + search_error);
    response.fallback = true;
    response.results = FallbackSearch(query, user_id, top_k);
    return response;
  }

  // Truncate qdrant results if no rerank
  if (!enable_rerank || !cfg_.enable_rerank || qdrant_results.size() <= 3) {
    const std::size_t limit = static_cast<std::size_t>(top_k);
    if (qdrant_results.size() > limit) qdrant_results.resize(limit);

    for (const auto& qr : qdrant_results) {
      SearchItem item;
      item.ppt_id       = qr.ppt_id;
      item.title        = qr.title;
      item.topic        = qr.topic;
      item.template_name = qr.template_name;
      item.pages        = qr.pages;
      item.created_at   = qr.created_at;
      item.status       = qr.status;
      item.score        = qr.score;
      response.results.push_back(std::move(item));
    }
    return response;
  }

  // Step 3: LLM Rerank
  std::vector<std::uint64_t> candidate_ids;
  std::vector<std::string> candidate_summaries;
  candidate_ids.reserve(qdrant_results.size());
  candidate_summaries.reserve(qdrant_results.size());

  for (const auto& qr : qdrant_results) {
    candidate_ids.push_back(qr.ppt_id);
    std::string summary = qr.title;
    if (!qr.topic.empty()) summary += "（" + qr.topic + "）";
    if (!qr.template_name.empty()) summary += " 模板：" + qr.template_name;
    candidate_summaries.push_back(summary);
  }

  const auto rerank_results = qwen_client_->RerankWithReason(
      query, candidate_ids, candidate_summaries, top_k, cfg_.rerank_model);

  if (rerank_results.empty()) {
    // Rerank failed, use raw vector results
    const std::size_t limit = static_cast<std::size_t>(top_k);
    for (std::size_t i = 0; i < qdrant_results.size() && i < limit; ++i) {
      const auto& qr = qdrant_results[i];
      SearchItem item;
      item.ppt_id       = qr.ppt_id;
      item.title        = qr.title;
      item.topic        = qr.topic;
      item.template_name = qr.template_name;
      item.pages        = qr.pages;
      item.created_at   = qr.created_at;
      item.status       = qr.status;
      item.score        = qr.score;
      response.results.push_back(std::move(item));
    }
    return response;
  }

  // Build a lookup map from qdrant results for metadata enrichment
  std::unordered_map<std::uint64_t, const QdrantClient::SearchResult*> meta_map;
  for (const auto& qr : qdrant_results) {
    meta_map[qr.ppt_id] = &qr;
  }

  for (const auto& rr : rerank_results) {
    SearchItem item;
    item.ppt_id = rr.ppt_id;
    item.reason = rr.reason;
    item.score  = rr.score;
    if (auto it = meta_map.find(rr.ppt_id); it != meta_map.end()) {
      const auto* qr = it->second;
      item.title        = qr->title;
      item.topic        = qr->topic;
      item.template_name = qr->template_name;
      item.pages        = qr->pages;
      item.created_at   = qr->created_at;
      item.status       = qr->status;
    }
    response.results.push_back(std::move(item));
  }
  return response;
}

std::vector<AiSearchService::SearchItem> AiSearchService::FallbackSearch(
    const std::string& query,
    std::uint64_t user_id,
    int top_k) const {
  std::vector<SearchItem> results;
  auto scoped_conn = pool_->GetConnection();
  MYSQL* conn = scoped_conn.Get();
  if (!conn) return results;

  // Escape query for LIKE
  std::string escaped(query.size() * 2 + 1, '\0');
  unsigned long escaped_len = mysql_real_escape_string(
      conn, &escaped[0], query.c_str(), static_cast<unsigned long>(query.size()));
  escaped.resize(escaped_len);

  const std::string sql =
      "SELECT id, title, topic, template_name, pages, created_at, status, output_path "
      "FROM ppt_requests WHERE user_id=" + std::to_string(user_id) +
      " AND status='completed'"
      " AND (title LIKE '%" + escaped + "%' OR topic LIKE '%" + escaped + "%')"
      " ORDER BY created_at DESC LIMIT " + std::to_string(top_k);

  MYSQL_RES* res = nullptr;
  if (mysql_query(conn, sql.c_str()) == 0) {
    res = mysql_store_result(conn);
  }

  if (!res) return results;

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    SearchItem item;
    item.ppt_id       = row[0] ? std::stoull(row[0]) : 0;
    item.title        = row[1] ? row[1] : "";
    item.topic        = row[2] ? row[2] : "";
    item.template_name = row[3] ? row[3] : "";
    item.pages        = row[4] ? std::stoi(row[4]) : 0;
    item.created_at   = row[5] ? row[5] : "";
    item.status       = row[6] ? row[6] : "";
    item.output_path  = row[7] ? row[7] : "";
    item.score        = 0.8;  // Placeholder score for keyword matches
    item.fallback     = true;
    results.push_back(std::move(item));
  }
  mysql_free_result(res);
  return results;
}

int AiSearchService::ReindexUser(std::uint64_t user_id, std::string& error) {
  struct PptMeta { std::uint64_t id; };
  std::vector<PptMeta> metas;

  {
    auto scoped = pool_->GetConnection();
    MYSQL* conn = scoped.Get();
    if (!conn) { error = "No DB connection"; return -1; }

    const std::string sql =
        "SELECT id FROM ppt_requests WHERE user_id=" + std::to_string(user_id) +
        " AND status='completed' ORDER BY id ASC";

    if (mysql_query(conn, sql.c_str()) == 0) {
      MYSQL_RES* res = mysql_store_result(conn);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0]) metas.push_back({std::stoull(row[0])});
        }
        mysql_free_result(res);
      }
    }
  }

  int count = 0;
  for (const auto& m : metas) {
    std::string idx_err;
    if (IndexPptRequest(m.id, user_id, idx_err)) {
      ++count;
    } else {
      Logger::Warn("ReindexUser: failed id=" + std::to_string(m.id) + ": " + idx_err);
    }
  }
  return count;
}

int AiSearchService::ReindexAll(std::string& error) {
  struct PptMeta { std::uint64_t id; std::uint64_t user_id; };
  std::vector<PptMeta> metas;

  {
    auto scoped = pool_->GetConnection();
    MYSQL* conn = scoped.Get();
    if (!conn) { error = "No DB connection"; return -1; }

    const std::string sql =
        "SELECT id, user_id FROM ppt_requests WHERE status='completed' ORDER BY id ASC";

    if (mysql_query(conn, sql.c_str()) == 0) {
      MYSQL_RES* res = mysql_store_result(conn);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1]) {
            metas.push_back({std::stoull(row[0]), std::stoull(row[1])});
          }
        }
        mysql_free_result(res);
      }
    }
  }

  int count = 0;
  for (const auto& m : metas) {
    std::string idx_err;
    if (IndexPptRequest(m.id, m.user_id, idx_err)) {
      ++count;
    } else {
      Logger::Warn("ReindexAll: failed id=" + std::to_string(m.id) + ": " + idx_err);
    }
  }
  return count;
}

int AiSearchService::GetIndexCount(std::uint64_t user_id, std::string& error) {
  if (!qdrant_client_ || !qdrant_client_->IsAvailable()) {
    error = "Qdrant not available";
    return -1;
  }
  return qdrant_client_->CountUserPoints(user_id, error);
}
