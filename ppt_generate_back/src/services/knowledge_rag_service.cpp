#include "services/knowledge_rag_service.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

/** Replace invalid UTF-8 bytes with U+FFFD so nlohmann::json::dump() never throws. */
std::string ToSafeUtf8(const std::string& input) {
  static const unsigned char kRepl[] = {0xEF, 0xBF, 0xBD};
  std::string out;
  out.reserve(input.size());
  const unsigned char* p   = reinterpret_cast<const unsigned char*>(input.data());
  const unsigned char* end = p + input.size();
  while (p < end) {
    unsigned char b = *p++;
    if (b <= 0x7F) { out.push_back(static_cast<char>(b)); continue; }
    int extra = 0;
    if      (b >= 0xC2 && b <= 0xDF) extra = 1;
    else if (b >= 0xE0 && b <= 0xEF) extra = 2;
    else if (b >= 0xF0 && b <= 0xF4) extra = 3;
    else { out.append(reinterpret_cast<const char*>(kRepl), 3); continue; }
    if (p + extra > end) { out.append(reinterpret_cast<const char*>(kRepl), 3); break; }
    bool ok = true;
    for (int i = 0; i < extra; ++i)
      if ((p[i] & 0xC0) != 0x80) { ok = false; break; }
    if (!ok) { out.append(reinterpret_cast<const char*>(kRepl), 3); continue; }
    out.push_back(static_cast<char>(b));
    for (int i = 0; i < extra; ++i) out.push_back(static_cast<char>(*p++));
  }
  return out;
}

// 将 chunk_id 字符串稳定地映射为 Qdrant 所需的 uint64 数值 ID。
// 格式：material_id__chunk_N  → 取 material_id 后 8 位 + chunk_index 低 8 位拼成 32 位数。
// 为避免碰撞，使用 FNV-1a 64-bit hash。
std::uint64_t ChunkIdToUint64(const std::string& chunk_id) {
  constexpr std::uint64_t kFnvBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  std::uint64_t hash = kFnvBasis;
  for (unsigned char c : chunk_id) {
    hash ^= c;
    hash *= kFnvPrime;
  }
  return hash;
}

// 按中文句号、英文句号、换行等自然边界切分文本为句子列表。
std::vector<std::string> SplitSentences(const std::string& text) {
  std::vector<std::string> sentences;
  std::string cur;
  // 遍历 UTF-8 字节，识别多字节中文标点
  const unsigned char* p   = reinterpret_cast<const unsigned char*>(text.data());
  const unsigned char* end = p + text.size();

  auto flush = [&]() {
    // 去除前后空白
    std::size_t s = cur.find_first_not_of(" \t\r\n");
    std::size_t e = cur.find_last_not_of(" \t\r\n");
    if (s != std::string::npos && e >= s) {
      sentences.push_back(cur.substr(s, e - s + 1));
    }
    cur.clear();
  };

  while (p < end) {
    unsigned char b = *p;

    // ASCII 字符
    if (b < 0x80) {
      cur.push_back(static_cast<char>(b));
      if (b == '\n' || b == '.' || b == '!' || b == '?') {
        flush();
      }
      ++p;
      continue;
    }

    // 多字节 UTF-8 序列
    int seq_len = 1;
    if ((b & 0xE0) == 0xC0)      seq_len = 2;
    else if ((b & 0xF0) == 0xE0) seq_len = 3;
    else if ((b & 0xF8) == 0xF0) seq_len = 4;

    if (p + seq_len > end) break;

    // 提取本字符的原始字节（用于标点识别）
    std::string ch(reinterpret_cast<const char*>(p), seq_len);
    cur += ch;

    // 中文句号 U+3002 = E3 80 82，全角感叹号 U+FF01 = EF BC 81，全角问号 U+FF1F = EF BC 9F
    if (seq_len == 3) {
      const unsigned char* q = p;
      if ((q[0] == 0xE3 && q[1] == 0x80 && q[2] == 0x82) ||  // 。
          (q[0] == 0xEF && q[1] == 0xBC && q[2] == 0x81) ||  // ！
          (q[0] == 0xEF && q[1] == 0xBC && q[2] == 0x9F)) {  // ？
        flush();
      }
    }
    p += seq_len;
  }
  flush();
  return sentences;
}

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

KnowledgeRagService::KnowledgeRagService(std::shared_ptr<QdrantClient> qdrant_client,
                                         std::shared_ptr<QwenClient> qwen_client,
                                         std::string qdrant_base_url,
                                         int chunk_size,
                                         int chunk_overlap,
                                         int vector_dim)
    : qdrant_client_(std::move(qdrant_client)),
      qwen_client_(std::move(qwen_client)),
      qdrant_base_url_(std::move(qdrant_base_url)),
      chunk_size_(chunk_size),
      chunk_overlap_(chunk_overlap),
      vector_dim_(vector_dim) {}

bool KnowledgeRagService::IsAvailable() const {
  return qdrant_client_ && qdrant_client_->IsAvailable() &&
         qwen_client_ && qwen_client_->IsEnabled();
}

bool KnowledgeRagService::EnsureCollection(std::string& error) {
  if (!IsAvailable()) {
    error = "KnowledgeRagService not available (Qdrant or QwenClient disabled)";
    return false;
  }

  int code = 0;
  const std::string check_path = "/collections/" + std::string(kCollectionName);
  DoQdrantRequest("GET", check_path, "", code);
  if (code == 200) {
    Logger::Info("KnowledgeRagService: collection '" +
                 std::string(kCollectionName) + "' already exists.");
    return true;
  }

  nlohmann::json body = {
    {"vectors", {
      {"size", vector_dim_},
      {"distance", "Cosine"}
    }}
  };
  const std::string resp = DoQdrantRequest("PUT", check_path, body.dump(), code);
  if (code == 200 || code == 201) {
    Logger::Info("KnowledgeRagService: collection '" +
                 std::string(kCollectionName) + "' created.");
    return true;
  }
  error = "Failed to create collection '" + std::string(kCollectionName) +
          "', HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

// ──────────────────────────────────────────────────────────────────────────────

int KnowledgeRagService::IndexMaterial(const std::string& material_id,
                                       std::uint64_t user_id,
                                       const std::string& extract_text,
                                       const std::string& filename,
                                       std::string& error) {
  if (!IsAvailable()) {
    error = "KnowledgeRagService not available";
    return -1;
  }
  if (extract_text.empty()) {
    error = "extract_text is empty";
    return -1;
  }

  // 先删除该素材的旧向量（幂等支持重建索引）
  std::string del_err;
  RemoveMaterial(material_id, del_err);

  const std::vector<std::string> chunks = SplitIntoChunks(extract_text);
  if (chunks.empty()) {
    error = "No chunks generated from extract_text";
    return -1;
  }

  const std::string safe_filename = ToSafeUtf8(filename);
  int success_count = 0;
  for (int i = 0; i < static_cast<int>(chunks.size()); ++i) {
    const std::string safe_chunk = ToSafeUtf8(chunks[i]);
    if (safe_chunk.empty()) continue;

    // 向量化
    const auto embedding = qwen_client_->GetEmbedding(safe_chunk);
    if (embedding.empty()) {
      Logger::Warn("KnowledgeRagService::IndexMaterial: embedding failed for chunk " +
                   std::to_string(i) + " of " + material_id);
      continue;
    }

    const std::string chunk_id = material_id + "__chunk_" + std::to_string(i);
    const std::uint64_t point_id = ChunkIdToUint64(chunk_id);

    // 构建 Qdrant point
    nlohmann::json vec_arr = nlohmann::json::array();
    for (float v : embedding) {
      vec_arr.push_back(v);
    }

    nlohmann::json point = {
      {"id", point_id},
      {"vector", vec_arr},
      {"payload", {
        {"chunk_id",    chunk_id},
        {"material_id", material_id},
        {"user_id",     user_id},
        {"chunk_index", i},
        {"chunk_text",  safe_chunk.substr(0, 1000)},
        {"filename",    safe_filename}
      }}
    };

    nlohmann::json body = {{"points", nlohmann::json::array({point})}};

    int code = 0;
    const std::string path = "/collections/" + std::string(kCollectionName) + "/points";
    const std::string resp = DoQdrantRequest("PUT", path, body.dump(), code);
    if (code == 200 || code == 201) {
      ++success_count;
    } else {
      Logger::Warn("KnowledgeRagService::IndexMaterial: upsert chunk " +
                   std::to_string(i) + " failed, HTTP " + std::to_string(code));
    }
  }

  Logger::Info("KnowledgeRagService::IndexMaterial: indexed " +
               std::to_string(success_count) + "/" + std::to_string(chunks.size()) +
               " chunks for material=" + material_id);
  return success_count;
}

// ──────────────────────────────────────────────────────────────────────────────

bool KnowledgeRagService::RemoveMaterial(const std::string& material_id,
                                         std::string& error) {
  if (!IsAvailable()) {
    error = "KnowledgeRagService not available";
    return false;
  }

  // 通过 payload filter 删除所有 material_id 匹配的点
  nlohmann::json body = {
    {"filter", {
      {"must", nlohmann::json::array({
        {{"key", "material_id"}, {"match", {{"value", material_id}}}}
      })}
    }}
  };

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points/delete";
  const std::string resp = DoQdrantRequest("POST", path, body.dump(), code);
  if (code == 200) {
    return true;
  }
  error = "RemoveMaterial failed, HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

// ──────────────────────────────────────────────────────────────────────────────

std::vector<KnowledgeRagService::KnowledgeChunk> KnowledgeRagService::Retrieve(
    const std::string& query,
    std::uint64_t user_id,
    const std::vector<std::string>& material_ids,
    int top_k) const {
  if (!IsAvailable() || query.empty()) {
    return {};
  }

  const auto query_vec = qwen_client_->GetEmbedding(query);
  if (query_vec.empty()) {
    Logger::Warn("KnowledgeRagService::Retrieve: embedding failed for query");
    return {};
  }

  nlohmann::json vec_arr = nlohmann::json::array();
  for (float v : query_vec) {
    vec_arr.push_back(v);
  }

  // 构建过滤条件：必须匹配 user_id，可选匹配 material_ids
  nlohmann::json must_clauses = nlohmann::json::array();
  must_clauses.push_back(
    {{"key", "user_id"}, {"match", {{"value", user_id}}}});

  if (!material_ids.empty()) {
    nlohmann::json any_values = nlohmann::json::array();
    for (const auto& mid : material_ids) {
      any_values.push_back(mid);
    }
    must_clauses.push_back(
      {{"key", "material_id"}, {"match", {{"any", any_values}}}});
  }

  nlohmann::json body = {
    {"vector", vec_arr},
    {"limit", top_k},
    {"score_threshold", 0.3},
    {"with_payload", true},
    {"filter", {{"must", must_clauses}}}
  };

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points/search";
  const std::string resp = DoQdrantRequest("POST", path, body.dump(), code);
  if (code != 200) {
    Logger::Warn("KnowledgeRagService::Retrieve: search failed, HTTP " +
                 std::to_string(code));
    return {};
  }

  std::vector<KnowledgeChunk> out;
  try {
    auto j = nlohmann::json::parse(resp);
    const auto& results = j.at("result");
    for (const auto& item : results) {
      KnowledgeChunk chunk;
      chunk.score = item.value("score", 0.0);
      const auto& payload = item.at("payload");
      chunk.chunk_id    = payload.value("chunk_id", "");
      chunk.material_id = payload.value("material_id", "");
      chunk.user_id     = payload.value("user_id", static_cast<std::uint64_t>(0));
      chunk.chunk_index = payload.value("chunk_index", 0);
      chunk.chunk_text  = payload.value("chunk_text", "");
      chunk.filename    = payload.value("filename", "");
      out.push_back(std::move(chunk));
    }
  } catch (const std::exception& ex) {
    Logger::Warn(std::string("KnowledgeRagService::Retrieve parse error: ") + ex.what());
  }
  return out;
}

// ──────────────────────────────────────────────────────────────────────────────

std::string KnowledgeRagService::FormatChunksAsContext(
    const std::vector<KnowledgeChunk>& chunks) {
  if (chunks.empty()) return {};

  std::ostringstream oss;
  oss << "【知识库参考资料】\n";
  oss << "以下内容来自用户上传的文档，请在生成时优先引用其中的具体数据和观点：\n\n";

  int idx = 1;
  for (const auto& chunk : chunks) {
    oss << "--- 参考片段 " << idx++ << " ---\n";
    if (!chunk.filename.empty()) {
      oss << "来源文档：" << chunk.filename << "\n";
    }
    oss << chunk.chunk_text << "\n\n";
  }
  return oss.str();
}

// ──────────────────────────────────────────────────────────────────────────────

int KnowledgeRagService::CountUserChunks(std::uint64_t user_id,
                                         std::string& error) const {
  if (!IsAvailable()) {
    error = "KnowledgeRagService not available";
    return -1;
  }

  nlohmann::json body = {
    {"filter", {
      {"must", nlohmann::json::array({
        {{"key", "user_id"}, {"match", {{"value", user_id}}}}
      })}
    }}
  };

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points/count";
  const std::string resp = DoQdrantRequest("POST", path, body.dump(), code);
  if (code != 200) {
    error = "CountUserChunks failed, HTTP " + std::to_string(code);
    return -1;
  }
  try {
    auto j = nlohmann::json::parse(resp);
    return j.at("result").value("count", 0);
  } catch (const std::exception& ex) {
    error = std::string("CountUserChunks parse error: ") + ex.what();
    return -1;
  }
}

// ──────────────────────────────────────────────────────────────────────────────

std::vector<std::string> KnowledgeRagService::SplitIntoChunks(
    const std::string& text) const {
  const auto sentences = SplitSentences(text);
  std::vector<std::string> chunks;

  std::string cur_chunk;
  int cur_len = 0;

  for (const auto& sent : sentences) {
    int sent_len = static_cast<int>(sent.size());

    if (cur_len + sent_len > chunk_size_ && !cur_chunk.empty()) {
      // 当前块已满，保存并开始新块（含 overlap）
      chunks.push_back(cur_chunk);

      // 保留末尾 overlap 字节作为新块开头
      if (chunk_overlap_ > 0 && cur_len > chunk_overlap_) {
        const std::size_t overlap_start =
            cur_chunk.size() > static_cast<std::size_t>(chunk_overlap_)
                ? cur_chunk.size() - static_cast<std::size_t>(chunk_overlap_)
                : 0;
        cur_chunk = cur_chunk.substr(overlap_start);
        cur_len   = static_cast<int>(cur_chunk.size());
      } else {
        cur_chunk.clear();
        cur_len = 0;
      }
    }

    if (!cur_chunk.empty()) cur_chunk += "；";
    cur_chunk += sent;
    cur_len = static_cast<int>(cur_chunk.size());
  }

  if (!cur_chunk.empty()) {
    chunks.push_back(cur_chunk);
  }
  return chunks;
}

// ──────────────────────────────────────────────────────────────────────────────

std::string KnowledgeRagService::ExtractIndexableText(
    const std::string& extract_result_json) {
  if (extract_result_json.empty()) return {};

  try {
    auto j = nlohmann::json::parse(extract_result_json);
    std::ostringstream oss;

    // summary
    if (j.contains("summary") && j["summary"].is_string()) {
      oss << j["summary"].get<std::string>() << "\n\n";
    }
    // key_points
    if (j.contains("key_points") && j["key_points"].is_array()) {
      for (const auto& kp : j["key_points"]) {
        if (kp.is_string()) oss << kp.get<std::string>() << "\n";
      }
      oss << "\n";
    }
    // data_mentions
    if (j.contains("data_mentions") && j["data_mentions"].is_array()) {
      for (const auto& dm : j["data_mentions"]) {
        if (dm.is_string()) oss << dm.get<std::string>() << "\n";
      }
      oss << "\n";
    }
    // outline
    if (j.contains("outline") && j["outline"].is_array()) {
      for (const auto& item : j["outline"]) {
        if (item.is_string()) {
          oss << item.get<std::string>() << "\n";
        } else if (item.is_object()) {
          if (item.contains("title") && item["title"].is_string())
            oss << item["title"].get<std::string>() << "\n";
          if (item.contains("points") && item["points"].is_array()) {
            for (const auto& pt : item["points"]) {
              if (pt.is_string()) oss << "  - " << pt.get<std::string>() << "\n";
            }
          }
        }
      }
    }

    return oss.str();
  } catch (const std::exception&) {
    // 若非 JSON，直接返回原文本
    return extract_result_json;
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// 内部 HTTP 辅助：直接操作 user_knowledge collection（不依赖 QdrantClient::collection_）

namespace {

std::size_t WriteCallbackKrag(void* contents, std::size_t size, std::size_t nmemb,
                              void* userp) {
  auto* buf = static_cast<std::string*>(userp);
  buf->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

}  // anonymous namespace

std::string KnowledgeRagService::DoQdrantRequest(const std::string& method,
                                                  const std::string& path,
                                                  const std::string& body,
                                                  int& http_code) const {
  CURL* curl = curl_easy_init();
  if (!curl) {
    http_code = 0;
    return {};
  }

  const std::string url = qdrant_base_url_ + path;
  std::string response;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackKrag);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

  if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
  }
  // GET is default

  CURLcode res = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  http_code = (res == CURLE_OK) ? static_cast<int>(code) : 0;
  return response;
}
