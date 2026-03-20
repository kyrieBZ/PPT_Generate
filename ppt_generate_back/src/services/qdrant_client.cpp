#include "services/qdrant_client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  auto* buf = static_cast<std::string*>(userp);
  buf->append(static_cast<char*>(contents), size * nmemb);
  return size * nmemb;
}

}  // namespace

QdrantClient::QdrantClient(const std::string& host, std::uint16_t port,
                           const std::string& collection, int vector_size,
                           int timeout_seconds)
    : base_url_("http://" + host + ":" + std::to_string(port)),
      collection_(collection),
      vector_size_(vector_size),
      timeout_seconds_(timeout_seconds) {
  // Probe availability with a lightweight health check
  int code = 0;
  const std::string resp = DoRequest("GET", "/healthz", "", code);
  available_ = (code == 200);
  if (!available_) {
    Logger::Warn("QdrantClient: server not available at " + base_url_ +
                 " (HTTP " + std::to_string(code) + ")");
  }
}

std::string QdrantClient::DoRequest(const std::string& method,
                                    const std::string& path,
                                    const std::string& body,
                                    int& http_code) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    http_code = 0;
    return {};
  }

  const std::string url = base_url_ + path;
  std::string response;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
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
    if (body.empty()) {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
  }
  // GET is the default

  CURLcode res = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  http_code = (res == CURLE_OK) ? static_cast<int>(code) : 0;
  return response;
}

bool QdrantClient::EnsureCollection(std::string& error) {
  // Check if collection exists
  int code = 0;
  const std::string path = "/collections/" + collection_;
  DoRequest("GET", path, "", code);
  if (code == 200) {
    return true;
  }

  // Create collection
  nlohmann::json body = {
    {"vectors", {
      {"size", vector_size_},
      {"distance", "Cosine"}
    }}
  };
  const std::string resp = DoRequest("PUT", path, body.dump(), code);
  if (code == 200 || code == 201) {
    Logger::Info("QdrantClient: collection '" + collection_ + "' created.");
    return true;
  }
  error = "Failed to create Qdrant collection '" + collection_ +
          "', HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

bool QdrantClient::UpsertPoint(std::uint64_t ppt_id,
                               std::uint64_t user_id,
                               const std::string& title,
                               const std::string& topic,
                               const std::string& template_name,
                               int pages,
                               const std::string& created_at,
                               const std::vector<float>& vector,
                               std::string& error) {
  if (vector.empty()) {
    error = "Empty embedding vector";
    return false;
  }

  nlohmann::json vec_arr = nlohmann::json::array();
  for (float v : vector) {
    vec_arr.push_back(v);
  }

  nlohmann::json point = {
    {"id", ppt_id},
    {"vector", vec_arr},
    {"payload", {
      {"ppt_id", ppt_id},
      {"user_id", user_id},
      {"title", title},
      {"topic", topic},
      {"template_name", template_name},
      {"pages", pages},
      {"created_at", created_at},
      {"status", "completed"}
    }}
  };

  nlohmann::json body = {
    {"points", nlohmann::json::array({point})}
  };

  int code = 0;
  const std::string path = "/collections/" + collection_ + "/points";
  const std::string resp = DoRequest("PUT", path, body.dump(), code);
  if (code == 200 || code == 201) {
    return true;
  }
  error = "UpsertPoint failed, HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

std::vector<QdrantClient::SearchResult> QdrantClient::Search(
    const std::vector<float>& query_vector,
    std::uint64_t user_id,
    int top_k,
    double score_threshold,
    std::string& error) {
  if (query_vector.empty()) {
    error = "Empty query vector";
    return {};
  }

  nlohmann::json vec_arr = nlohmann::json::array();
  for (float v : query_vector) {
    vec_arr.push_back(v);
  }

  nlohmann::json body = {
    {"vector", vec_arr},
    {"limit", top_k},
    {"score_threshold", score_threshold},
    {"with_payload", true},
    {"filter", {
      {"must", nlohmann::json::array({
        {{"key", "user_id"}, {"match", {{"value", user_id}}}}
      })}
    }}
  };

  int code = 0;
  const std::string path = "/collections/" + collection_ + "/points/search";
  const std::string resp = DoRequest("POST", path, body.dump(), code);
  if (code != 200) {
    error = "Search failed, HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
    return {};
  }

  try {
    auto j = nlohmann::json::parse(resp);
    const auto& results = j.at("result");
    std::vector<SearchResult> out;
    out.reserve(results.size());
    for (const auto& item : results) {
      SearchResult r;
      r.score = item.value("score", 0.0);
      const auto& payload = item.at("payload");
      r.ppt_id       = payload.value("ppt_id", static_cast<std::uint64_t>(0));
      r.user_id      = payload.value("user_id", static_cast<std::uint64_t>(0));
      r.title        = payload.value("title", "");
      r.topic        = payload.value("topic", "");
      r.template_name = payload.value("template_name", "");
      r.pages        = payload.value("pages", 0);
      r.created_at   = payload.value("created_at", "");
      r.status       = payload.value("status", "");
      out.push_back(std::move(r));
    }
    return out;
  } catch (const std::exception& ex) {
    error = std::string("Search parse error: ") + ex.what();
    return {};
  }
}

bool QdrantClient::DeletePoint(std::uint64_t ppt_id, std::string& error) {
  nlohmann::json body = {
    {"points", nlohmann::json::array({ppt_id})}
  };

  int code = 0;
  const std::string path = "/collections/" + collection_ + "/points/delete";
  const std::string resp = DoRequest("POST", path, body.dump(), code);
  if (code == 200) {
    return true;
  }
  error = "DeletePoint failed, HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

int QdrantClient::CountUserPoints(std::uint64_t user_id, std::string& error) {
  nlohmann::json body = {
    {"filter", {
      {"must", nlohmann::json::array({
        {{"key", "user_id"}, {"match", {{"value", user_id}}}}
      })}
    }}
  };

  int code = 0;
  const std::string path = "/collections/" + collection_ + "/points/count";
  const std::string resp = DoRequest("POST", path, body.dump(), code);
  if (code != 200) {
    error = "CountUserPoints failed, HTTP " + std::to_string(code);
    return -1;
  }
  try {
    auto j = nlohmann::json::parse(resp);
    return j.at("result").value("count", 0);
  } catch (const std::exception& ex) {
    error = std::string("CountUserPoints parse error: ") + ex.what();
    return -1;
  }
}
