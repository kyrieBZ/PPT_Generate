#include "services/doubao_image_client.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace {
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total_size = size * nmemb;
  auto* buffer = static_cast<std::string*>(userp);
  buffer->append(static_cast<char*>(contents), total_size);
  return total_size;
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool ExtractImagesFromData(const nlohmann::json& data,
                           std::vector<DoubaoImageAsset>& out) {
  if (!data.is_array()) {
    return false;
  }
  bool found = false;
  for (const auto& item : data) {
    if (!item.is_object()) {
      continue;
    }
    DoubaoImageAsset asset;
    if (auto it = item.find("url"); it != item.end() && it->is_string()) {
      asset.url = it->get<std::string>();
      found = true;
    }
    if (auto it = item.find("b64_json"); it != item.end() && it->is_string()) {
      asset.b64_json = it->get<std::string>();
      found = true;
    }
    if (!asset.url.empty() || !asset.b64_json.empty()) {
      out.push_back(std::move(asset));
    }
  }
  return found;
}

bool ExtractImages(const nlohmann::json& response,
                   std::vector<DoubaoImageAsset>& out) {
  if (response.is_object()) {
    if (auto it = response.find("data"); it != response.end()) {
      if (ExtractImagesFromData(*it, out)) {
        return true;
      }
    }
    if (auto it = response.find("images"); it != response.end() && it->is_array()) {
      for (const auto& item : *it) {
        if (item.is_string()) {
          DoubaoImageAsset asset;
          asset.url = item.get<std::string>();
          out.push_back(std::move(asset));
        }
      }
      return !out.empty();
    }
    if (auto it = response.find("output"); it != response.end() && it->is_object()) {
      if (auto images = it->find("images"); images != it->end() && images->is_array()) {
        for (const auto& item : *images) {
          if (item.is_string()) {
            DoubaoImageAsset asset;
            asset.url = item.get<std::string>();
            out.push_back(std::move(asset));
          } else if (item.is_object()) {
            DoubaoImageAsset asset;
            if (auto url_it = item.find("url"); url_it != item.end() && url_it->is_string()) {
              asset.url = url_it->get<std::string>();
            }
            if (auto b64_it = item.find("b64_json"); b64_it != item.end() && b64_it->is_string()) {
              asset.b64_json = b64_it->get<std::string>();
            }
            if (!asset.url.empty() || !asset.b64_json.empty()) {
              out.push_back(std::move(asset));
            }
          }
        }
        return !out.empty();
      }
    }
  }
  return false;
}
}  // namespace

DoubaoImageClient::DoubaoImageClient(ProviderConfig config) : config_(std::move(config)) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

bool DoubaoImageClient::IsEnabled() const {
  return !config_.doubao_api_key.empty() && !config_.doubao_image_endpoint.empty();
}

std::uint32_t DoubaoImageClient::timeout_seconds() const {
  return config_.doubao_timeout_seconds > 0 ? config_.doubao_timeout_seconds : 30;
}

bool DoubaoImageClient::GenerateImages(const std::string& prompt,
                                       std::vector<DoubaoImageAsset>& out_assets,
                                       std::string& error_message) const {
  out_assets.clear();
  if (!IsEnabled()) {
    error_message = "Doubao image API未配置";
    return false;
  }
  if (prompt.empty()) {
    error_message = "图片描述为空";
    return false;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "无法初始化HTTP客户端";
    return false;
  }

  nlohmann::json body;
  if (!config_.doubao_image_model.empty()) {
    body["model"] = config_.doubao_image_model;
  }
  body["prompt"] = prompt;
  if (!config_.doubao_image_size.empty()) {
    body["size"] = config_.doubao_image_size;
  }
  if (!config_.doubao_image_response_format.empty()) {
    body["response_format"] = config_.doubao_image_response_format;
  }
  body["n"] = std::max<std::uint32_t>(1, config_.doubao_image_count);

  std::string response_buffer;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth_header = "Authorization: Bearer " + config_.doubao_api_key;
  headers = curl_slist_append(headers, auth_header.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, config_.doubao_image_endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  const auto payload = body.dump();
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.doubao_timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error_message = curl_easy_strerror(res);
    return false;
  }
  if (http_code < 200 || http_code >= 300) {
    error_message = "Doubao image API返回HTTP " + std::to_string(http_code);
    return false;
  }

  try {
    auto response_json = nlohmann::json::parse(response_buffer);
    if (response_json.contains("error")) {
      if (response_json["error"].is_string()) {
        error_message = response_json["error"].get<std::string>();
      } else if (response_json["error"].is_object()) {
        error_message = response_json["error"].value("message", "Doubao image API错误");
      } else {
        error_message = "Doubao image API错误";
      }
      return false;
    }
    if (response_json.contains("code") && response_json.contains("message")) {
      error_message = response_json.value("message", "Doubao image API错误");
      return false;
    }
    if (!ExtractImages(response_json, out_assets) || out_assets.empty()) {
      error_message = "未解析到图片结果";
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}
