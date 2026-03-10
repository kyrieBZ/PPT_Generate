#include "services/wanxiang_image_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  const auto total = size * nmemb;
  auto* buffer = static_cast<std::string*>(userp);
  buffer->append(static_cast<char*>(contents), total);
  return total;
}

std::string Trim(std::string value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::string DefaultWanxImageEndpoint() {
  // 异步图像生成接口，参考官方文档
  return "https://dashscope.aliyuncs.com/api/v1/services/aigc/image-generation/generation";
}

std::string BuildTasksEndpoint(const std::string& image_endpoint,
                               const std::string& task_id) {
  // 从 image_endpoint 中截取到 /api/v1 作为基础地址，然后拼接 /tasks/{task_id}
  auto pos = image_endpoint.find("/api/v1");
  std::string base;
  if (pos == std::string::npos) {
    base = "https://dashscope.aliyuncs.com/api/v1";
  } else {
    base = image_endpoint.substr(0, pos + std::string("/api/v1").size());
  }
  if (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return base + "/tasks/" + task_id;
}

bool ExtractTaskId(const nlohmann::json& response, std::string& task_id) {
  if (!response.is_object()) {
    return false;
  }
  if (auto output = response.find("output"); output != response.end() && output->is_object()) {
    if (auto it = output->find("task_id"); it != output->end() && it->is_string()) {
      task_id = it->get<std::string>();
      task_id = Trim(task_id);
      return !task_id.empty();
    }
  }
  // 一些实现可能直接在顶层返回 task_id
  if (auto it = response.find("task_id"); it != response.end() && it->is_string()) {
    task_id = Trim(it->get<std::string>());
    return !task_id.empty();
  }
  return false;
}

bool ExtractImageUrlsFromTask(const nlohmann::json& response,
                              std::vector<std::string>& out_urls) {
  if (!response.is_object()) {
    return false;
  }
  const nlohmann::json* output = nullptr;
  if (auto it = response.find("output"); it != response.end() && it->is_object()) {
    output = &*it;
  } else {
    output = &response;
  }
  if (!output->is_object()) {
    return false;
  }
  auto it_choices = output->find("choices");
  if (it_choices == output->end() || !it_choices->is_array()) {
    return false;
  }
  bool found = false;
  for (const auto& choice : *it_choices) {
    if (!choice.is_object()) {
      continue;
    }
    auto msg_it = choice.find("message");
    if (msg_it == choice.end() || !msg_it->is_object()) {
      continue;
    }
    auto content_it = msg_it->find("content");
    if (content_it == msg_it->end() || !content_it->is_array()) {
      continue;
    }
    for (const auto& item : *content_it) {
      if (!item.is_object()) {
        continue;
      }
      auto type_it = item.find("type");
      auto image_it = item.find("image");
      if (type_it != item.end() && image_it != item.end() &&
          type_it->is_string() && image_it->is_string() &&
          type_it->get<std::string>() == "image") {
        auto url = Trim(image_it->get<std::string>());
        if (!url.empty()) {
          out_urls.push_back(url);
          found = true;
        }
      }
    }
  }
  return found;
}

bool HasError(const nlohmann::json& response, std::string& message) {
  if (!response.is_object()) {
    return false;
  }
  if (auto it = response.find("error"); it != response.end()) {
    if (it->is_string()) {
      message = it->get<std::string>();
      return true;
    }
    if (it->is_object()) {
      message = it->value("message", "Wanxiang image API error");
      return true;
    }
  }
  if (auto it = response.find("code"); it != response.end()) {
    // 通用 code/message 错误结构
    std::string code = it->is_string() ? it->get<std::string>() : "";
    std::string msg = response.value("message", "");
    if (!code.empty() || !msg.empty()) {
      if (!code.empty() && !msg.empty()) {
        message = code + ": " + msg;
      } else if (!msg.empty()) {
        message = msg;
      } else {
        message = "Wanxiang image API error";
      }
      return true;
    }
  }
  return false;
}

}  // namespace

WanxiangImageClient::WanxiangImageClient(ProviderConfig config) : config_(std::move(config)) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

bool WanxiangImageClient::IsEnabled() const {
  return !config_.qwen_api_key.empty();
}

std::uint32_t WanxiangImageClient::timeout_seconds() const {
  return config_.wanx_timeout_seconds > 0 ? config_.wanx_timeout_seconds : 120;
}

bool WanxiangImageClient::GenerateImages(const std::string& prompt,
                                         std::vector<std::string>& out_urls,
                                         std::string& error_message) const {
  out_urls.clear();
  if (!IsEnabled()) {
    error_message = "Wanxiang image API 未配置（缺少 qwen_api_key）";
    return false;
  }
  if (prompt.empty()) {
    error_message = "图片描述为空";
    return false;
  }

  const std::string endpoint =
      config_.wanx_image_endpoint.empty() ? DefaultWanxImageEndpoint() : config_.wanx_image_endpoint;

  // Step 1: 创建异步任务
  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "无法初始化HTTP客户端";
    return false;
  }

  nlohmann::json body;
  body["model"] = config_.wanx_image_model.empty() ? "wan2.6-image" : config_.wanx_image_model;
  body["input"] = {
      {"messages",
       nlohmann::json::array(
           {nlohmann::json{{"role", "user"},
                           {"content", nlohmann::json::array({nlohmann::json{{"text", prompt}}})}}})}};
  nlohmann::json params;
  params["enable_interleave"] = true;
  params["max_images"] = 1;
  params["size"] = "1280*720";
  body["parameters"] = std::move(params);

  std::string response_buffer;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth_header = "Authorization: Bearer " + config_.qwen_api_key;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, "X-DashScope-Async: enable");

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  const auto payload = body.dump();
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds()));
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
  try {
    auto response_json = nlohmann::json::parse(response_buffer);
    if (HasError(response_json, error_message)) {
      return false;
    }
    if (http_code < 200 || http_code >= 300) {
      if (error_message.empty()) {
        error_message = "Wanxiang image API 返回 HTTP " + std::to_string(http_code);
      }
      return false;
    }
    std::string task_id;
    if (!ExtractTaskId(response_json, task_id)) {
      error_message = "Wanxiang image API 未返回 task_id";
      return false;
    }

    // Step 2: 轮询任务结果
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(timeout_seconds());
    const std::string task_endpoint = BuildTasksEndpoint(endpoint, task_id);

    while (std::chrono::steady_clock::now() < deadline) {
      CURL* curl_get = curl_easy_init();
      if (!curl_get) {
        error_message = "无法初始化HTTP客户端";
        return false;
      }
      std::string task_buffer;
      struct curl_slist* headers_get = nullptr;
      headers_get = curl_slist_append(headers_get, "Content-Type: application/json");
      std::string auth_header_get = "Authorization: Bearer " + config_.qwen_api_key;
      headers_get = curl_slist_append(headers_get, auth_header_get.c_str());

      curl_easy_setopt(curl_get, CURLOPT_URL, task_endpoint.c_str());
      curl_easy_setopt(curl_get, CURLOPT_HTTPHEADER, headers_get);
      curl_easy_setopt(curl_get, CURLOPT_HTTPGET, 1L);
      curl_easy_setopt(curl_get, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl_get, CURLOPT_WRITEDATA, &task_buffer);
      curl_easy_setopt(curl_get, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds()));
      curl_easy_setopt(curl_get, CURLOPT_CONNECTTIMEOUT, 5L);

      CURLcode res_get = curl_easy_perform(curl_get);
      long http_code_get = 0;
      curl_easy_getinfo(curl_get, CURLINFO_RESPONSE_CODE, &http_code_get);
      curl_slist_free_all(headers_get);
      curl_easy_cleanup(curl_get);

      if (res_get != CURLE_OK) {
        error_message = curl_easy_strerror(res_get);
        return false;
      }
      auto task_json = nlohmann::json::parse(task_buffer, nullptr, false);
      if (task_json.is_discarded()) {
        error_message = "无法解析 Wanxiang 任务响应";
        return false;
      }
      if (HasError(task_json, error_message)) {
        return false;
      }
      if (http_code_get < 200 || http_code_get >= 300) {
        if (error_message.empty()) {
          error_message = "查询 Wanxiang 任务失败，HTTP " + std::to_string(http_code_get);
        }
        return false;
      }

      const nlohmann::json* output = nullptr;
      if (auto it = task_json.find("output"); it != task_json.end() && it->is_object()) {
        output = &*it;
      } else {
        output = &task_json;
      }
      auto status_it = output->find("task_status");
      std::string status = status_it != output->end() && status_it->is_string()
                               ? status_it->get<std::string>()
                               : "";
      if (status == "SUCCEEDED") {
        if (!ExtractImageUrlsFromTask(*output, out_urls) || out_urls.empty()) {
          error_message = "Wanxiang 任务成功但未返回图片URL";
          return false;
        }
        return true;
      }
      if (status == "FAILED" || status == "CANCELED") {
        error_message = "Wanxiang 任务失败，状态：" + status;
        return false;
      }

      std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    error_message = "Wanxiang 任务超时";
    return false;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}

