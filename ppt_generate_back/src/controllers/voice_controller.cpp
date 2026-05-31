#include "controllers/voice_controller.h"

#include <algorithm>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

std::size_t WriteCallback(void* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
  const std::size_t total = size * nmemb;
  static_cast<std::string*>(userdata)->append(static_cast<char*>(ptr), total);
  return total;
}

/** 将字符串转为小写 */
std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

/** 从 multipart Content-Type 中提取 boundary */
std::string ExtractBoundary(const std::string& content_type) {
  const std::string prefix = "boundary=";
  auto pos = content_type.find(prefix);
  if (pos == std::string::npos) return {};
  std::string boundary = content_type.substr(pos + prefix.size());
  // 去除前后引号或空白
  while (!boundary.empty() && (boundary.front() == '"' || boundary.front() == ' '))
    boundary.erase(boundary.begin());
  while (!boundary.empty() && (boundary.back() == '"' || boundary.back() == ' '))
    boundary.pop_back();
  return boundary;
}

/**
 * 简单 multipart/form-data 解析器
 * 只提取名为 "audio" 的 part，返回其原始二进制数据和 Content-Type。
 */
bool ParseMultipart(const std::string& body,
                    const std::string& boundary,
                    std::string& audio_data,
                    std::string& part_content_type) {
  if (boundary.empty()) return false;

  const std::string delimiter = "--" + boundary;
  const std::string final_delimiter = delimiter + "--";

  std::size_t pos = 0;
  while (pos < body.size()) {
    // 找下一个 delimiter
    auto delim_pos = body.find(delimiter, pos);
    if (delim_pos == std::string::npos) break;

    // 跳过 delimiter 及其后的 \r\n
    pos = delim_pos + delimiter.size();
    if (pos + 1 < body.size() && body[pos] == '-' && body[pos + 1] == '-') break; // final delimiter
    if (pos < body.size() && body[pos] == '\r') pos++;
    if (pos < body.size() && body[pos] == '\n') pos++;

    // 读取 headers
    std::string part_headers;
    auto header_end = body.find("\r\n\r\n", pos);
    if (header_end == std::string::npos) break;
    part_headers = body.substr(pos, header_end - pos);
    pos = header_end + 4;

    // 找下一个 delimiter（确定 part body 的范围）
    auto next_delim = body.find("\r\n" + delimiter, pos);
    if (next_delim == std::string::npos) break;

    const std::string part_body = body.substr(pos, next_delim - pos);
    pos = next_delim + 2; // skip \r\n

    // 检查 Content-Disposition 是否包含 name="audio"
    const std::string lower_headers = ToLower(part_headers);
    if (lower_headers.find("name=\"audio\"") == std::string::npos &&
        lower_headers.find("name=audio") == std::string::npos) {
      continue;
    }

    // 提取 Content-Type（如果有）
    auto ct_pos = lower_headers.find("content-type:");
    if (ct_pos != std::string::npos) {
      auto eol = part_headers.find('\n', ct_pos);
      if (eol != std::string::npos) {
        std::string ct_line = part_headers.substr(ct_pos + 13, eol - ct_pos - 13);
        // trim
        while (!ct_line.empty() && (ct_line.back() == '\r' || ct_line.back() == ' '))
          ct_line.pop_back();
        while (!ct_line.empty() && ct_line.front() == ' ')
          ct_line.erase(ct_line.begin());
        part_content_type = ct_line;
      }
    }

    audio_data = part_body;
    return true;
  }
  return false;
}

/**
 * 从 Content-Type 或文件名推断音频格式
 * 返回阿里云 NLS 支持的格式字符串：pcm / wav / opus / amr / mp3 / aac / m4a
 */
std::string InferFormat(const std::string& part_ct) {
  const std::string ct = ToLower(part_ct);
  if (ct.find("wav") != std::string::npos)  return "wav";
  if (ct.find("mp3") != std::string::npos || ct.find("mpeg") != std::string::npos) return "mp3";
  if (ct.find("opus") != std::string::npos) return "opus";
  if (ct.find("amr") != std::string::npos)  return "amr";
  if (ct.find("aac") != std::string::npos)  return "aac";
  if (ct.find("m4a") != std::string::npos || ct.find("mp4") != std::string::npos) return "m4a";
  if (ct.find("ogg") != std::string::npos)  return "wav"; // ogg/opus fallback
  // 浏览器 MediaRecorder 默认产出 webm/opus，识别为 opus
  if (ct.find("webm") != std::string::npos) return "opus";
  return "wav"; // safe default
}

/**
 * 通过 AccessKey 直接获取 Token（Token API）
 * 阿里云 NLS 需要先用 AccessKey 换取 Token，Token 有效期 24 小时。
 * 这里每次请求都重新拿 Token（生产环境应缓存）。
 */
bool FetchNlsToken(const std::string& access_key_id,
                   const std::string& access_key_secret,
                   std::string& token,
                   std::string& error) {
  // 阿里云 Token API（RAM STS 短期 token，公共端点）
  const std::string url = "https://nls-meta.cn-shanghai.aliyuncs.com/pop/2018-05-18/tokens";

  CURL* curl = curl_easy_init();
  if (!curl) { error = "curl_easy_init failed"; return false; }

  std::string response_body;
  std::string post_fields; // POST body 为空，鉴权在 Header 中

  // 需要的 Header：Authorization 使用 AK/SK 做 HMAC-SHA256 签名（较复杂）
  // 阿里云 NLS Token API 支持直接用 AK/SK 在请求参数中鉴权（老版 HTTP API 风格）
  // 使用更简单的方式：POST JSON，Header 中带 AK/SK
  const nlohmann::json req_body = nlohmann::json::object();
  const std::string body_str = req_body.dump();

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  const std::string auth_header = "X-NLS-AccessKeyId: " + access_key_id;
  const std::string secret_header = "X-NLS-AccessKeySecret: " + access_key_secret;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, secret_header.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error = std::string("获取语音Token失败: ") + curl_easy_strerror(res);
    return false;
  }

  try {
    const auto j = nlohmann::json::parse(response_body);
    if (j.contains("Token") && j["Token"].is_object()) {
      const auto& tok = j["Token"];
      if (tok.contains("Id") && tok["Id"].is_string()) {
        token = tok["Id"].get<std::string>();
        return true;
      }
    }
    if (j.contains("token") && j["token"].is_string()) {
      token = j["token"].get<std::string>();
      return true;
    }
    error = "Token API 响应格式异常: " + response_body.substr(0, 300);
    return false;
  } catch (const std::exception& e) {
    error = std::string("Token JSON 解析失败: ") + e.what();
    return false;
  }
}

}  // namespace

VoiceController::VoiceController(std::shared_ptr<AuthService> auth_service,
                                 const AsrConfig& asr_config)
    : auth_service_(std::move(auth_service)), asr_config_(asr_config) {}

std::string VoiceController::ExtractToken(const HttpRequest& request) {
  const std::string auth_header = request.Header("authorization");
  if (auth_header.size() > 7 && auth_header.substr(0, 7) == "Bearer ") {
    return auth_header.substr(7);
  }
  return {};
}

bool VoiceController::ParseMultipartAudio(const HttpRequest& request,
                                          std::string& audio_data,
                                          std::string& content_type,
                                          std::string& error) {
  const std::string ct_header = request.Header("content-type");
  const std::string lower_ct = ToLower(ct_header);

  if (lower_ct.find("multipart/form-data") == std::string::npos) {
    error = "Content-Type 必须为 multipart/form-data";
    return false;
  }

  const std::string boundary = ExtractBoundary(ct_header);
  if (boundary.empty()) {
    error = "multipart boundary 缺失";
    return false;
  }

  if (!ParseMultipart(request.body, boundary, audio_data, content_type)) {
    error = "未找到 audio 字段，请确保 FormData 中的字段名为 audio";
    return false;
  }

  if (audio_data.empty()) {
    error = "音频数据为空";
    return false;
  }

  return true;
}

bool VoiceController::CallAliyunAsr(const std::string& audio_data,
                                    const std::string& audio_format,
                                    std::string& result_text,
                                    std::string& error) {
  // 获取 NLS Token
  std::string nls_token;
  std::string token_error;
  if (!FetchNlsToken(asr_config_.access_key_id,
                     asr_config_.access_key_secret,
                     nls_token, token_error)) {
    error = token_error;
    return false;
  }

  // 构建 URL：NLS Flash（一句话识别）HTTP API
  // 文档：https://help.aliyun.com/zh/isi/developer-reference/quick-start-of-flash-recognizer
  std::ostringstream url_oss;
  url_oss << asr_config_.endpoint
          << "?appkey=" << asr_config_.app_key
          << "&format=" << audio_format
          << "&sample_rate=" << asr_config_.sample_rate
          << "&enable_punctuation_prediction=true"
          << "&enable_inverse_text_normalization=true";
  const std::string url = url_oss.str();

  CURL* curl = curl_easy_init();
  if (!curl) { error = "curl_easy_init failed"; return false; }

  std::string response_body;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
  const std::string token_header = "X-NLS-Token: " + nls_token;
  headers = curl_slist_append(headers, token_header.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, audio_data.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(audio_data.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(asr_config_.timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode res = curl_easy_perform(curl);

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error = std::string("语音识别 HTTP 请求失败: ") + curl_easy_strerror(res);
    return false;
  }

  try {
    const auto j = nlohmann::json::parse(response_body);

    // 成功响应：{ "status": 20000000, "result": "识别文本" }
    if (j.contains("status")) {
      const int status = j["status"].is_number() ? j["status"].get<int>() : -1;
      if (status == 20000000) {
        result_text = j.value("result", "");
        return true;
      }
      const std::string msg = j.value("message", j.value("error_message", "未知错误"));
      error = "阿里云语音识别失败 (status=" + std::to_string(status) + "): " + msg;
      return false;
    }

    // 兼容其他格式
    if (j.contains("result") && j["result"].is_string()) {
      result_text = j["result"].get<std::string>();
      return true;
    }

    error = "语音识别响应格式异常: " + response_body.substr(0, 300);
    return false;
  } catch (const std::exception& e) {
    error = std::string("语音识别响应解析失败: ") + e.what() +
            " | 原始响应: " + response_body.substr(0, 200);
    return false;
  }
}

// ── POST /api/voice/transcribe ────────────────────────────────────────────────
HttpResponse VoiceController::Transcribe(const HttpRequest& request) {
  HttpResponse response;
  response.ApplyCors();

  // 鉴权
  const std::string token = ExtractToken(request);
  if (token.empty()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "请先登录"));
  }
  std::string auth_error;
  const auto user_opt = auth_service_->GetUserFromToken(token, auth_error);
  if (!user_opt.has_value()) {
    return HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "登录已过期，请重新登录"));
  }

  // 检查 ASR 是否启用
  if (!asr_config_.enabled) {
    return HttpResponse::Json(503, ErrorJson("SERVICE_UNAVAILABLE",
        "语音识别服务未启用，请在配置文件中设置 asr.enabled=true 并配置阿里云密钥"));
  }

  if (asr_config_.access_key_id.empty() || asr_config_.access_key_secret.empty() ||
      asr_config_.app_key.empty()) {
    return HttpResponse::Json(503, ErrorJson("SERVICE_UNAVAILABLE",
        "语音识别服务配置不完整，请检查 access_key_id / access_key_secret / app_key"));
  }

  // 解析音频数据
  std::string audio_data;
  std::string part_content_type;
  std::string parse_error;
  if (!ParseMultipartAudio(request, audio_data, part_content_type, parse_error)) {
    return HttpResponse::Json(400, ErrorJson("BAD_REQUEST", parse_error));
  }

  // 推断格式
  const std::string audio_format = InferFormat(part_content_type);

  Logger::Info("VoiceController: 收到语音识别请求，格式=" + audio_format +
               " 大小=" + std::to_string(audio_data.size()) + "B");

  // 调用阿里云 ASR
  std::string result_text;
  std::string asr_error;
  if (!CallAliyunAsr(audio_data, audio_format, result_text, asr_error)) {
    Logger::Error("VoiceController: 语音识别失败: " + asr_error);
    return HttpResponse::Json(502, ErrorJson("ASR_ERROR", asr_error));
  }

  Logger::Info("VoiceController: 识别结果=\"" + result_text + "\"");
  return HttpResponse::Json(200, {{"text", result_text}});
}
