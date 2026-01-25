#include "services/s3_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "logger.h"

namespace {
struct ParsedEndpoint {
  std::string scheme = "http";
  std::string host_port;
  std::string base_path;
};

ParsedEndpoint ParseEndpoint(const std::string& endpoint) {
  ParsedEndpoint result;
  std::string value = endpoint;
  if (value.empty()) {
    return result;
  }
  const auto scheme_pos = value.find("://");
  if (scheme_pos != std::string::npos) {
    result.scheme = value.substr(0, scheme_pos);
    value = value.substr(scheme_pos + 3);
  }
  const auto path_pos = value.find('/');
  if (path_pos != std::string::npos) {
    result.host_port = value.substr(0, path_pos);
    result.base_path = value.substr(path_pos);
  } else {
    result.host_port = value;
  }
  if (!result.base_path.empty() && result.base_path.back() == '/') {
    result.base_path.pop_back();
  }
  return result;
}

bool IsUnreserved(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

std::string UrlEncode(const std::string& value, bool encode_slash) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (unsigned char ch : value) {
    if (IsUnreserved(static_cast<char>(ch)) || (!encode_slash && ch == '/')) {
      out << static_cast<char>(ch);
    } else {
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

std::string CanonicalPath(const std::string& base_path,
                          const std::string& bucket,
                          const std::string& object_key) {
  std::string path = base_path;
  if (path == "/") {
    path.clear();
  }
  if (!path.empty()) {
    if (path.front() != '/') {
      path.insert(path.begin(), '/');
    }
    while (path.size() > 1 && path.back() == '/') {
      path.pop_back();
    }
  }

  std::string full = path;
  full += "/";
  full += UrlEncode(bucket, true);
  if (!object_key.empty()) {
    full += "/";
    full += UrlEncode(object_key, false);
  }
  if (full.empty() || full.front() != '/') {
    full.insert(full.begin(), '/');
  }
  return full;
}

std::string HexEncode(const unsigned char* data, std::size_t len) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < len; ++i) {
    out << std::setw(2) << static_cast<int>(data[i]);
  }
  return out.str();
}

std::string Sha256Hex(const std::string& data) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
  return HexEncode(hash, SHA256_DIGEST_LENGTH);
}

std::string HmacSha256(const std::string& key, const std::string& data) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int result_len = 0;
  HMAC(EVP_sha256(),
       reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(),
       result, &result_len);
  return std::string(reinterpret_cast<const char*>(result), result_len);
}

std::string BuildCanonicalQuery(const std::vector<std::pair<std::string, std::string>>& params) {
  std::vector<std::pair<std::string, std::string>> sorted = params;
  std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.first == rhs.first) {
      return lhs.second < rhs.second;
    }
    return lhs.first < rhs.first;
  });
  std::ostringstream out;
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    if (i > 0) {
      out << '&';
    }
    out << UrlEncode(sorted[i].first, true) << '=' << UrlEncode(sorted[i].second, true);
  }
  return out.str();
}

std::string FormatTime(std::time_t value, const char* format) {
  std::tm tm_snapshot{};
#if defined(_WIN32)
  gmtime_s(&tm_snapshot, &value);
#else
  gmtime_r(&value, &tm_snapshot);
#endif
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), format, &tm_snapshot) == 0) {
    return {};
  }
  return buffer;
}

std::once_flag curl_once;
void EnsureCurlInit() {
  std::call_once(curl_once, []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  });
}
}  // namespace

S3Client::S3Client(S3Config config) : config_(std::move(config)) {}

bool S3Client::IsEnabled() const {
  return config_.enabled();
}

std::uint32_t S3Client::UrlExpirationSeconds() const {
  return config_.url_expiration_seconds;
}

std::string S3Client::PresignGetUrl(const std::string& object_key) const {
  return PresignUrl("GET", object_key, config_.url_expiration_seconds, config_.effective_public_endpoint());
}

std::string S3Client::PresignUrl(const std::string& method,
                                 const std::string& object_key,
                                 std::uint32_t expires_seconds,
                                 const std::string& endpoint_override) const {
  if (!IsEnabled()) {
    return {};
  }
  const auto endpoint = ParseEndpoint(endpoint_override);
  if (endpoint.host_port.empty()) {
    return {};
  }

  const auto now = std::time(nullptr);
  const auto amz_date = FormatTime(now, "%Y%m%dT%H%M%SZ");
  const auto date_stamp = FormatTime(now, "%Y%m%d");
  const std::string service = "s3";
  const std::string scope = date_stamp + "/" + config_.region + "/" + service + "/aws4_request";
  const std::string credential = config_.access_key + "/" + scope;

  std::vector<std::pair<std::string, std::string>> query_params = {
      {"X-Amz-Algorithm", "AWS4-HMAC-SHA256"},
      {"X-Amz-Credential", credential},
      {"X-Amz-Date", amz_date},
      {"X-Amz-Expires", std::to_string(expires_seconds)},
      {"X-Amz-SignedHeaders", "host"}};

  const auto canonical_query = BuildCanonicalQuery(query_params);
  const auto canonical_uri = CanonicalPath(endpoint.base_path, config_.bucket, object_key);
  const std::string canonical_headers = "host:" + endpoint.host_port + "\n";
  const std::string signed_headers = "host";
  const std::string payload_hash = "UNSIGNED-PAYLOAD";

  std::ostringstream canonical_request;
  canonical_request << method << '\n'
                    << canonical_uri << '\n'
                    << canonical_query << '\n'
                    << canonical_headers << '\n'
                    << signed_headers << '\n'
                    << payload_hash;

  const std::string canonical_hash = Sha256Hex(canonical_request.str());
  std::ostringstream string_to_sign;
  string_to_sign << "AWS4-HMAC-SHA256\n"
                 << amz_date << '\n'
                 << scope << '\n'
                 << canonical_hash;

  const auto k_date = HmacSha256("AWS4" + config_.secret_key, date_stamp);
  const auto k_region = HmacSha256(k_date, config_.region);
  const auto k_service = HmacSha256(k_region, service);
  const auto k_signing = HmacSha256(k_service, "aws4_request");
  const auto final_signature = HmacSha256(k_signing, string_to_sign.str());
  const auto signature_hex = HexEncode(reinterpret_cast<const unsigned char*>(final_signature.data()),
                                       final_signature.size());

  std::ostringstream url;
  url << endpoint.scheme << "://" << endpoint.host_port << canonical_uri
      << "?" << canonical_query
      << "&X-Amz-Signature=" << signature_hex;
  return url.str();
}

bool S3Client::UploadFile(const std::string& local_path,
                          const std::string& object_key,
                          std::string& error) const {
  if (!IsEnabled()) {
    error = "S3 is not configured";
    return false;
  }
  if (local_path.empty()) {
    error = "Local path is empty";
    return false;
  }
  const auto upload_url = PresignUrl("PUT", object_key, config_.url_expiration_seconds, config_.endpoint);
  if (upload_url.empty()) {
    error = "Failed to generate upload URL";
    return false;
  }

  std::FILE* file = std::fopen(local_path.c_str(), "rb");
  if (!file) {
    error = "Unable to open local file";
    return false;
  }
  std::fseek(file, 0, SEEK_END);
  const auto size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(file);
    error = "Local file is empty";
    return false;
  }

  EnsureCurlInit();
  CURL* curl = curl_easy_init();
  if (!curl) {
    std::fclose(file);
    error = "Unable to init curl";
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());
  curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(curl, CURLOPT_READDATA, file);
  curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);
  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  curl_easy_cleanup(curl);
  std::fclose(file);

  if (res != CURLE_OK) {
    error = std::string("Upload failed: ") + curl_easy_strerror(res);
    return false;
  }
  if (response_code < 200 || response_code >= 300) {
    error = "Upload failed with HTTP status " + std::to_string(response_code);
    return false;
  }
  return true;
}

bool S3Client::DeleteObject(const std::string& object_key,
                            std::string& error) const {
  if (!IsEnabled()) {
    error = "S3 is not configured";
    return false;
  }
  if (object_key.empty()) {
    error = "Object key is empty";
    return false;
  }
  const auto delete_url = PresignUrl("DELETE", object_key, config_.url_expiration_seconds, config_.endpoint);
  if (delete_url.empty()) {
    error = "Failed to generate delete URL";
    return false;
  }

  EnsureCurlInit();
  CURL* curl = curl_easy_init();
  if (!curl) {
    error = "Unable to init curl";
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, delete_url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);
  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error = std::string("Delete failed: ") + curl_easy_strerror(res);
    return false;
  }
  if (response_code < 200 || response_code >= 300) {
    error = "Delete failed with HTTP status " + std::to_string(response_code);
    return false;
  }
  return true;
}
