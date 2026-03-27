#include "services/template_recommend_service.h"

#include <functional>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

// FNV-1a 64-bit hash — 将模板 ID 字符串映射为 Qdrant 所需的 uint64 数值 ID
std::uint64_t TemplateIdToUint64(const std::string& id) {
  constexpr std::uint64_t kBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t h = kBasis;
  for (unsigned char c : id) {
    h ^= c;
    h *= kPrime;
  }
  return h;
}

std::size_t WriteCallbackTR(void* contents, std::size_t size, std::size_t nmemb,
                            void* userp) {
  auto* buf = static_cast<std::string*>(userp);
  buf->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

TemplateRecommendService::TemplateRecommendService(std::shared_ptr<QwenClient> qwen_client,
                                                   std::string qdrant_base_url,
                                                   int vector_dim)
    : qwen_client_(std::move(qwen_client)),
      qdrant_base_url_(std::move(qdrant_base_url)),
      vector_dim_(vector_dim) {}

bool TemplateRecommendService::IsAvailable() const {
  return qwen_client_ && qwen_client_->IsEnabled() && !qdrant_base_url_.empty();
}

// ──────────────────────────────────────────────────────────────────────────────

bool TemplateRecommendService::EnsureCollection(std::string& error) {
  if (!IsAvailable()) {
    error = "TemplateRecommendService not available (QwenClient disabled or no Qdrant URL)";
    return false;
  }

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName);
  DoQdrantRequest("GET", path, "", code);
  if (code == 200) {
    Logger::Info("TemplateRecommendService: collection '" +
                 std::string(kCollectionName) + "' already exists.");
    return true;
  }

  nlohmann::json body = {
    {"vectors", {
      {"size", vector_dim_},
      {"distance", "Cosine"}
    }}
  };
  const std::string resp = DoQdrantRequest("PUT", path, body.dump(), code);
  if (code == 200 || code == 201) {
    Logger::Info("TemplateRecommendService: collection '" +
                 std::string(kCollectionName) + "' created.");
    return true;
  }
  error = "Failed to create ppt_templates collection, HTTP " +
          std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

// ──────────────────────────────────────────────────────────────────────────────

/* static */
std::string TemplateRecommendService::BuildIndexText(const TemplateInfo& tmpl) {
  std::ostringstream oss;
  oss << tmpl.name << "\n";
  oss << tmpl.description << "\n";
  for (const auto& tag : tmpl.tags) {
    oss << tag << " ";
  }
  return oss.str();
}

int TemplateRecommendService::IndexTemplates(const std::vector<TemplateInfo>& templates,
                                             std::string& error) {
  if (!IsAvailable()) {
    error = "TemplateRecommendService not available";
    return -1;
  }
  if (templates.empty()) {
    error = "templates list is empty";
    return 0;
  }

  // 批量 upsert：每次最多 64 个点，避免请求体过大
  constexpr int kBatchSize = 64;
  int total_indexed = 0;

  for (std::size_t start = 0; start < templates.size(); start += kBatchSize) {
    const std::size_t end = std::min(start + kBatchSize, templates.size());
    nlohmann::json points = nlohmann::json::array();

    for (std::size_t i = start; i < end; ++i) {
      const auto& tmpl = templates[i];
      const std::string index_text = BuildIndexText(tmpl);
      const auto vec = qwen_client_->GetEmbedding(index_text);
      if (vec.empty()) {
        Logger::Warn("TemplateRecommendService: embedding failed for template id=" + tmpl.id);
        continue;
      }

      nlohmann::json tags_arr = nlohmann::json::array();
      for (const auto& t : tmpl.tags) tags_arr.push_back(t);

      nlohmann::json point = {
        {"id", TemplateIdToUint64(tmpl.id)},
        {"vector", vec},
        {"payload", {
          {"template_id",    tmpl.id},
          {"name",           tmpl.name},
          {"description",    tmpl.description},
          {"tags",           tags_arr},
          {"primary_color",  tmpl.primary_color},
          {"accent_color",   tmpl.accent_color},
          {"preview_image",  tmpl.preview_image},
          {"provider",       tmpl.provider}
        }}
      };
      points.push_back(std::move(point));
      ++total_indexed;
    }

    if (points.empty()) continue;

    nlohmann::json body = {{"points", points}};
    int code = 0;
    const std::string path = "/collections/" + std::string(kCollectionName) + "/points";
    const std::string resp = DoQdrantRequest("PUT", path, body.dump(), code);
    if (code != 200 && code != 201) {
      Logger::Warn("TemplateRecommendService: upsert batch failed HTTP " +
                   std::to_string(code) + ": " + resp.substr(0, 200));
    }
  }

  Logger::Info("TemplateRecommendService: indexed " + std::to_string(total_indexed) +
               " templates into '" + std::string(kCollectionName) + "'");
  return total_indexed;
}

// ──────────────────────────────────────────────────────────────────────────────

std::vector<TemplateRecommendService::RecommendResult>
TemplateRecommendService::Recommend(const std::string& topic, int top_k) const {
  if (!IsAvailable() || topic.empty()) return {};

  // 1. 将主题向量化
  const auto query_vec = qwen_client_->GetEmbedding(topic);
  if (query_vec.empty()) {
    Logger::Warn("TemplateRecommendService::Recommend: embedding failed for topic=" + topic);
    return {};
  }

  // 2. 向量检索 Qdrant ppt_templates，取 top_k * 2 候选（留余量给 reason 过滤）
  const int search_k = std::min(top_k * 2, 20);
  nlohmann::json search_body = {
    {"vector", query_vec},
    {"limit", search_k},
    {"with_payload", true},
    {"score_threshold", 0.3}
  };

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points/search";
  const std::string resp = DoQdrantRequest("POST", path, search_body.dump(), code);

  if (code != 200) {
    Logger::Warn("TemplateRecommendService::Recommend: Qdrant search failed HTTP " +
                 std::to_string(code));
    return {};
  }

  std::vector<RecommendResult> results;
  try {
    const auto j = nlohmann::json::parse(resp);
    if (!j.contains("result") || !j["result"].is_array()) return {};

    for (const auto& hit : j["result"]) {
      if (!hit.contains("payload")) continue;
      const auto& payload = hit["payload"];

      RecommendResult r;
      r.template_id   = payload.value("template_id",   "");
      r.name          = payload.value("name",           "");
      r.description   = payload.value("description",    "");
      r.primary_color = payload.value("primary_color",  "#1e293b");
      r.accent_color  = payload.value("accent_color",   "#0ea5e9");
      r.preview_image = payload.value("preview_image",  "");
      r.provider      = payload.value("provider",       "");
      r.score         = hit.value("score", 0.0);

      // 基于标签拼出 fallback reason（Qwen 调用前的临时占位）
      if (payload.contains("tags") && payload["tags"].is_array()) {
        std::string tag_str;
        for (const auto& t : payload["tags"]) {
          if (t.is_string()) {
            if (!tag_str.empty()) tag_str += "、";
            tag_str += t.get<std::string>();
          }
        }
        if (!tag_str.empty()) {
          r.match_reason = "该模板标签（" + tag_str + "）与您的主题匹配";
        }
      }

      if (!r.template_id.empty()) results.push_back(std::move(r));
      if (static_cast<int>(results.size()) >= top_k) break;
    }
  } catch (const std::exception& e) {
    Logger::Warn("TemplateRecommendService::Recommend: parse error: " + std::string(e.what()));
    return {};
  }

  if (results.empty()) return results;

  // 3. 用 Qwen 生成更丰富的 match_reason（一次请求批量处理）
  EnrichWithReasons(topic, results);

  return results;
}

// ──────────────────────────────────────────────────────────────────────────────

void TemplateRecommendService::EnrichWithReasons(const std::string& topic,
                                                 std::vector<RecommendResult>& results) const {
  if (!qwen_client_ || results.empty()) return;

  // 构建 Prompt，让 Qwen 为每个候选模板生成一句推荐理由（JSON 数组格式）
  std::ostringstream prompt;
  prompt << "用户正在制作主题为「" << topic << "」的PPT演示文稿。\n";
  prompt << "以下是候选PPT模板列表（JSON数组），请为每个模板生成一句中文推荐理由，\n";
  prompt << "说明该模板为何适合此主题，约20-40字，直接返回JSON数组（与输入顺序一致）：\n\n";

  nlohmann::json candidates = nlohmann::json::array();
  for (const auto& r : results) {
    candidates.push_back({
      {"name",        r.name},
      {"description", r.description}
    });
  }
  prompt << candidates.dump(2);
  prompt << "\n\n请直接返回 JSON 数组，格式：[\"理由1\", \"理由2\", ...]，不要有任何解释。";

  // 复用 RerankWithReason 的内部 Qwen Chat API
  // 由于 QwenClient 没有通用的 Chat 接口，我们直接用 curl 调用 DashScope API
  // 这里用一个简单的结构：借助现有 GenerateOutline 的 Qwen API key 方式
  // 为了不破坏 QwenClient 接口，通过 curl 直接调用
  const std::string api_key = [&]() -> std::string {
    // QwenClient 没有暴露 api_key，通过判断 IsEnabled() + 尝试 GetEmbedding 无法获取
    // 改用 GenerateOutline 的失败提示来判断可用性；实际 key 通过 RerankWithReason 获取
    // 最简单方式：直接从 GetEmbedding 的成功状态判断可用，key 不对外暴露
    // 因此这里通过另一种方式：让 EnrichWithReasons 直接调用 RerankWithReason
    return "";
  }();
  (void)api_key;

  // 使用 QwenClient::RerankWithReason 的逻辑类似，但用于模板推荐
  // RerankWithReason 接受 ppt_id 列表，此场景改为用模板 ID 的哈希值
  std::vector<std::uint64_t> candidate_ids;
  std::vector<std::string> candidate_summaries;
  for (const auto& r : results) {
    candidate_ids.push_back(TemplateIdToUint64(r.template_id));
    candidate_summaries.push_back(r.name + "：" + r.description);
  }

  const auto rerank_results = qwen_client_->RerankWithReason(
      topic, candidate_ids, candidate_summaries,
      static_cast<int>(results.size()), "qwen-turbo");

  // 将 reason 映射回结果（按 candidate id 对应）
  for (const auto& rr : rerank_results) {
    for (auto& res : results) {
      if (TemplateIdToUint64(res.template_id) == rr.ppt_id && !rr.reason.empty()) {
        res.match_reason = rr.reason;
        break;
      }
    }
  }
}

// ──────────────────────────────────────────────────────────────────────────────

int TemplateRecommendService::CountIndexed(std::string& error) const {
  if (!IsAvailable()) {
    error = "service not available";
    return -1;
  }

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName);
  const std::string resp = DoQdrantRequest("GET", path, "", code);
  if (code != 200) {
    error = "HTTP " + std::to_string(code);
    return -1;
  }
  try {
    const auto j = nlohmann::json::parse(resp);
    return j.value(nlohmann::json::json_pointer("/result/points_count"), -1);
  } catch (...) {
    error = "parse error";
    return -1;
  }
}

// ──────────────────────────────────────────────────────────────────────────────

std::string TemplateRecommendService::DoQdrantRequest(const std::string& method,
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
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackTR);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

  if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (!body.empty()) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  }
  // GET 为默认

  curl_easy_perform(curl);
  long lcode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &lcode);
  http_code = static_cast<int>(lcode);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return response;
}
