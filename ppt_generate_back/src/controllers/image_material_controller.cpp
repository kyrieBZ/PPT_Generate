#include "controllers/image_material_controller.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"
#include "utils/upload_debug_log.h"

namespace {

std::string HexPreview(const std::string& value, std::size_t limit = 64) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  const std::size_t count = std::min(limit, value.size());
  for (std::size_t i = 0; i < count; ++i) {
    if (i > 0) oss << ' ';
    oss << std::setw(2)
        << static_cast<unsigned int>(static_cast<unsigned char>(value[i]));
  }
  if (value.size() > limit) {
    oss << " ...";
  }
  return oss.str();
}

bool IsContinuationByte(unsigned char c) {
  return (c & 0xC0) == 0x80;
}

bool NextUtf8Sequence(const std::string& input, std::size_t index, std::size_t& length) {
  if (index >= input.size()) return false;

  const unsigned char lead = static_cast<unsigned char>(input[index]);
  if (lead <= 0x7F) {
    length = 1;
    return true;
  }
  if (lead >= 0xC2 && lead <= 0xDF) {
    if (index + 1 >= input.size()) return false;
    if (!IsContinuationByte(static_cast<unsigned char>(input[index + 1]))) return false;
    length = 2;
    return true;
  }
  if (lead >= 0xE0 && lead <= 0xEF) {
    if (index + 2 >= input.size()) return false;
    const unsigned char c1 = static_cast<unsigned char>(input[index + 1]);
    const unsigned char c2 = static_cast<unsigned char>(input[index + 2]);
    const bool valid_second =
        (lead == 0xE0) ? (c1 >= 0xA0 && c1 <= 0xBF)
                       : (lead == 0xED) ? (c1 >= 0x80 && c1 <= 0x9F)
                                        : IsContinuationByte(c1);
    if (!valid_second || !IsContinuationByte(c2)) return false;
    length = 3;
    return true;
  }
  if (lead >= 0xF0 && lead <= 0xF4) {
    if (index + 3 >= input.size()) return false;
    const unsigned char c1 = static_cast<unsigned char>(input[index + 1]);
    const unsigned char c2 = static_cast<unsigned char>(input[index + 2]);
    const unsigned char c3 = static_cast<unsigned char>(input[index + 3]);
    const bool valid_second =
        (lead == 0xF0) ? (c1 >= 0x90 && c1 <= 0xBF)
                       : (lead == 0xF4) ? (c1 >= 0x80 && c1 <= 0x8F)
                                        : IsContinuationByte(c1);
    if (!valid_second || !IsContinuationByte(c2) || !IsContinuationByte(c3)) return false;
    length = 4;
    return true;
  }
  return false;
}

bool IsValidUtf8(const std::string& input) {
  for (std::size_t i = 0; i < input.size();) {
    std::size_t seq_len = 0;
    if (!NextUtf8Sequence(input, i, seq_len)) return false;
    i += seq_len;
  }
  return true;
}

std::string EscapeInvalidUtf8(const std::string& input) {
  std::ostringstream oss;
  oss << std::hex << std::uppercase << std::setfill('0');
  for (std::size_t i = 0; i < input.size();) {
    std::size_t seq_len = 0;
    if (NextUtf8Sequence(input, i, seq_len)) {
      oss.write(input.data() + static_cast<std::streamoff>(i),
                static_cast<std::streamsize>(seq_len));
      i += seq_len;
      continue;
    }
    oss << "\\x" << std::setw(2)
        << static_cast<unsigned int>(static_cast<unsigned char>(input[i]));
    ++i;
  }
  return oss.str();
}

std::string JsonSafeString(const std::string& value, const std::string& field_name) {
  if (IsValidUtf8(value)) return value;
  upload_debug_log::Append(
      "json_safe field=" + field_name +
      " invalid_utf8 hex=" + HexPreview(value, 96));
  return EscapeInvalidUtf8(value);
}

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string Trim(const std::string& s) {
  const auto start = s.find_first_not_of(" \t\r\n\"'");
  if (start == std::string::npos) return {};
  const auto end = s.find_last_not_of(" \t\r\n\"'");
  return s.substr(start, end - start + 1);
}

std::string ExtractBoundary(const std::string& content_type) {
  const std::string key = "boundary=";
  const auto pos = content_type.find(key);
  if (pos == std::string::npos) return {};
  std::string boundary = content_type.substr(pos + key.size());
  if (!boundary.empty() && boundary.front() == '"') {
    boundary = boundary.substr(1);
    const auto q = boundary.find('"');
    if (q != std::string::npos) boundary.resize(q);
  }
  const auto semi = boundary.find(';');
  if (semi != std::string::npos) boundary.resize(semi);
  return Trim(boundary);
}

struct MultipartPart {
  std::string name;
  std::string filename;
  std::string filename_star;
  std::string content_type;
  std::string data;
};

bool PercentDecode(const std::string& input, std::string& output) {
  output.clear();
  output.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const char hi = input[i + 1];
      const char lo = input[i + 2];
      if (!std::isxdigit(static_cast<unsigned char>(hi)) ||
          !std::isxdigit(static_cast<unsigned char>(lo))) {
        return false;
      }
      const std::string hex = input.substr(i + 1, 2);
      output.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
      i += 2;
      continue;
    }
    output.push_back(input[i]);
  }
  return true;
}

bool DecodeRfc5987Filename(const std::string& raw_value, std::string& decoded) {
  const std::string trimmed = Trim(raw_value);
  const auto first_quote = trimmed.find('\'');
  if (first_quote == std::string::npos) return false;
  const auto second_quote = trimmed.find('\'', first_quote + 1);
  if (second_quote == std::string::npos) return false;

  const std::string charset = ToLower(trimmed.substr(0, first_quote));
  const std::string encoded = trimmed.substr(second_quote + 1);
  std::string bytes;
  if (!PercentDecode(encoded, bytes)) return false;

  if (!charset.empty() && charset != "utf-8") {
    return false;
  }
  if (!IsValidUtf8(bytes)) return false;
  decoded = bytes;
  return true;
}

std::string LeafFilename(const std::string& filename) {
  const auto pos = filename.find_last_of("/\\");
  if (pos == std::string::npos) return filename;
  return filename.substr(pos + 1);
}

std::vector<MultipartPart> ParseMultipart(const std::string& body, const std::string& boundary) {
  std::vector<MultipartPart> parts;
  if (boundary.empty()) return parts;

  const std::string delimiter = "--" + boundary;
  std::size_t pos = 0;
  while (pos < body.size()) {
    const auto delim_pos = body.find(delimiter, pos);
    if (delim_pos == std::string::npos) break;

    std::size_t after_delim = delim_pos + delimiter.size();
    if (after_delim + 1 < body.size() && body[after_delim] == '-' && body[after_delim + 1] == '-') break;
    if (after_delim < body.size() && body[after_delim] == '\r') after_delim++;
    if (after_delim < body.size() && body[after_delim] == '\n') after_delim++;

    const auto header_end = body.find("\r\n\r\n", after_delim);
    if (header_end == std::string::npos) break;

    const std::string headers_section = body.substr(after_delim, header_end - after_delim);
    const std::size_t data_start = header_end + 4;
    const auto next_delim = body.find("\r\n" + delimiter, data_start);
    const std::size_t data_end = (next_delim == std::string::npos) ? body.size() : next_delim;

    MultipartPart part;
    part.data = body.substr(data_start, data_end - data_start);

    std::istringstream hstream(headers_section);
    std::string hline;
    while (std::getline(hstream, hline)) {
      if (!hline.empty() && hline.back() == '\r') hline.pop_back();
      const auto colon = hline.find(':');
      if (colon == std::string::npos) continue;
      const auto hkey = ToLower(Trim(hline.substr(0, colon)));
      const auto hval = Trim(hline.substr(colon + 1));
      if (hkey == "content-disposition") {
        std::istringstream dstream(hval);
        std::string token;
        while (std::getline(dstream, token, ';')) {
          token = Trim(token);
          if (token.rfind("name=", 0) == 0) {
            part.name = Trim(token.substr(5));
          } else if (token.rfind("filename*=", 0) == 0) {
            part.filename_star = Trim(token.substr(10));
          } else if (token.rfind("filename=", 0) == 0) {
            part.filename = Trim(token.substr(9));
          }
        }
      } else if (hkey == "content-type") {
        part.content_type = hval;
      }
    }
    if (!part.name.empty()) parts.push_back(std::move(part));
    pos = data_end;
  }
  return parts;
}

std::string SanitizeFilename(const std::string& name) {
  std::string result;
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.') result.push_back(static_cast<char>(c));
    else result.push_back('_');
  }
  if (result.empty()) result = "image";
  if (result.size() > 200) result.resize(200);
  return result;
}

std::string GetExtension(const std::string& filename) {
  const auto dot = filename.rfind('.');
  if (dot == std::string::npos) return {};
  std::string ext = filename.substr(dot + 1);
  return ToLower(ext);
}

std::string JoinAllowedTypes(const std::vector<std::string>& allowed_types) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < allowed_types.size(); ++i) {
    if (i > 0) oss << ", ";
    oss << allowed_types[i];
  }
  return oss.str();
}

bool IsAllowedExtension(const std::string& ext,
                        const std::vector<std::string>& allowed_types) {
  for (const auto& t : allowed_types) {
    if (t == ext) return true;
  }
  return false;
}

nlohmann::json ImageToJson(const ImageMaterialService::ImageMaterial& m) {
  nlohmann::json j;
  j["id"] = m.id;
  j["userId"] = m.user_id;
  j["filename"] = JsonSafeString(m.filename, "image.filename");
  j["originalFilename"] = JsonSafeString(m.original_filename, "image.original_filename");
  j["description"] = JsonSafeString(m.description, "image.description");
  j["tags"] = JsonSafeString(m.tags, "image.tags");
  j["status"] = JsonSafeString(m.status, "image.status");
  j["errorMsg"] = JsonSafeString(m.error_msg, "image.error_msg");
  j["fileSize"] = m.file_size;
  j["createdAt"] = m.created_at;
  j["updatedAt"] = m.updated_at;
  return j;
}

std::string NormalizeUploadedFilename(const MultipartPart& part) {
  std::string filename = LeafFilename(part.filename);
  if (!part.filename_star.empty()) {
    std::string decoded;
    if (DecodeRfc5987Filename(part.filename_star, decoded)) {
      upload_debug_log::Append(
          "filename_star decoded raw=" + JsonSafeString(part.filename_star, "filename_star.raw") +
          " decoded=" + JsonSafeString(decoded, "filename_star.decoded"));
      filename = LeafFilename(decoded);
    } else {
      upload_debug_log::Append(
          "filename_star decode_failed raw=" + JsonSafeString(part.filename_star, "filename_star.raw") +
          " hex=" + HexPreview(part.filename_star, 96));
    }
  }

  if (!IsValidUtf8(filename)) {
    upload_debug_log::Append(
        "filename invalid_utf8 raw_hex=" + HexPreview(filename, 96));
    filename = EscapeInvalidUtf8(filename);
  }
  return filename;
}

void LogMultipartSummary(const HttpRequest& request,
                         const std::string& boundary,
                         const std::vector<MultipartPart>& parts) {
  upload_debug_log::Append(
      "upload_request content_type=" + JsonSafeString(request.Header("content-type"), "request.content_type") +
      " boundary=" + JsonSafeString(boundary, "request.boundary") +
      " body_size=" + std::to_string(request.body.size()) +
      " part_count=" + std::to_string(parts.size()));
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto& part = parts[i];
    upload_debug_log::Append(
        "multipart_part index=" + std::to_string(i) +
        " name=" + JsonSafeString(part.name, "part.name") +
        " filename=" + JsonSafeString(part.filename, "part.filename") +
        " filename_hex=" + HexPreview(part.filename, 96) +
        " filename_star=" + JsonSafeString(part.filename_star, "part.filename_star") +
        " content_type=" + JsonSafeString(part.content_type, "part.content_type") +
        " data_size=" + std::to_string(part.data.size()));
  }
}

std::shared_ptr<User> AuthenticateRequest(const HttpRequest& request,
                                          AuthService& auth_service,
                                          std::string& error) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    header = header.substr(7);
  }
  if (header.empty()) {
    error = "Token not provided";
    return nullptr;
  }
  auto user = auth_service.GetUserFromToken(header, error);
  if (!user) {
    error = error.empty() ? "Invalid token" : error;
    return nullptr;
  }
  return std::make_shared<User>(*user);
}
}  // namespace (anon)

ImageMaterialController::ImageMaterialController(
    std::shared_ptr<AuthService> auth_service,
    std::shared_ptr<ImageMaterialService> image_material_service,
    std::shared_ptr<ThreadPool> thread_pool,
    std::uint64_t max_image_size_mb,
    const std::vector<std::string>& allowed_types)
    : auth_service_(std::move(auth_service)),
      image_material_service_(std::move(image_material_service)),
      thread_pool_(std::move(thread_pool)),
      max_image_size_mb_(max_image_size_mb),
      allowed_types_(allowed_types) {}

HttpResponse ImageMaterialController::Upload(const HttpRequest& request) {
  std::string error;
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_SERVICE_UNAVAILABLE",
        "图片素材服务未启用"));
  }

  const auto ct_it = request.headers.find("content-type");
  if (ct_it == request.headers.end()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 Content-Type 头"));
  }
  const std::string boundary = ExtractBoundary(ct_it->second);
  if (boundary.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "无法解析 multipart boundary"));
  }

  const auto parts = ParseMultipart(request.body, boundary);
  LogMultipartSummary(request, boundary, parts);
  const MultipartPart* file_part = nullptr;
  for (const auto& p : parts) {
    if (p.name == "file" && !p.filename.empty()) {
      file_part = &p;
      break;
    }
  }
  if (!file_part) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST",
        "缺少 file 字段，请提供图片文件"));
  }

  const std::string original_filename = NormalizeUploadedFilename(*file_part);
  upload_debug_log::Append(
      "single_upload normalized_filename=" +
      JsonSafeString(original_filename, "single_upload.original_filename"));

  if (file_part->data.size() > max_image_size_mb_ * 1024 * 1024) {
    return HttpResponse::Json(413, ErrorJson("ERR_PAYLOAD_TOO_LARGE",
        "图片大小超过限制（" + std::to_string(max_image_size_mb_) + "MB）"));
  }

  const std::string ext = GetExtension(original_filename);
  bool allowed = false;
  for (const auto& t : allowed_types_) {
    if (t == ext) { allowed = true; break; }
  }
  if (!allowed) {
    return HttpResponse::Json(415, ErrorJson("ERR_UNSUPPORTED_TYPE",
        "不支持的图片格式，允许：jpg, jpeg, png, webp, gif"));
  }

  // 保存文件
  const std::string& upload_dir = image_material_service_->upload_dir();
  std::error_code ec;
  std::filesystem::create_directories(upload_dir, ec);

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string safe_name = SanitizeFilename(original_filename);
  const std::string stored_name = std::to_string(user->id) + "_" +
      std::to_string(now_ms) + "_" + safe_name;
  const std::string storage_path = upload_dir + "/" + stored_name;

  std::ofstream ofs(storage_path, std::ios::binary);
  if (!ofs) {
    Logger::Error("ImageMaterialController::Upload: cannot write " + storage_path);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "文件保存失败"));
  }
  ofs.write(file_part->data.data(), static_cast<std::streamsize>(file_part->data.size()));
  ofs.close();

  ImageMaterialService::ImageMaterial mat;
  std::string create_error;
  if (!image_material_service_->Create(user->id, stored_name, original_filename,
                                       storage_path, file_part->data.size(), mat, create_error)) {
    std::filesystem::remove(storage_path, ec);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "数据库写入失败：" + create_error));
  }

  // 异步分析与索引
  const std::string image_id = mat.id;
  auto svc = image_material_service_;
  if (thread_pool_) {
    thread_pool_->EnqueueDetached([image_id, svc]() mutable {
      svc->AnalyzeAndIndex(image_id);
    });
  }

  nlohmann::json resp;
  resp["success"] = true;
  resp["message"] = "上传成功，正在分析图片内容";
  resp["image"] = ImageToJson(mat);
  return HttpResponse::Json(201, resp);
}

HttpResponse ImageMaterialController::BatchUpload(const HttpRequest& request) {
  std::string error;
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_SERVICE_UNAVAILABLE",
        "图片素材服务未启用"));
  }

  const auto ct_it = request.headers.find("content-type");
  if (ct_it == request.headers.end()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 Content-Type 头"));
  }
  const std::string boundary = ExtractBoundary(ct_it->second);
  if (boundary.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "无法解析 multipart boundary"));
  }

  const auto parts = ParseMultipart(request.body, boundary);
  LogMultipartSummary(request, boundary, parts);
  std::vector<const MultipartPart*> file_parts;
  file_parts.reserve(parts.size());
  for (const auto& p : parts) {
    if ((p.name == "file" || p.name == "files[]" || p.name == "files") &&
        !p.filename.empty()) {
      file_parts.push_back(&p);
    }
  }
  if (file_parts.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST",
        "缺少 files[] 或 file 字段，请提供图片文件"));
  }

  constexpr std::size_t kMaxBatch = 20;
  if (file_parts.size() > kMaxBatch) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST",
        "单次最多上传 " + std::to_string(kMaxBatch) + " 张图片"));
  }

  const std::string& upload_dir = image_material_service_->upload_dir();
  std::error_code ec;
  std::filesystem::create_directories(upload_dir, ec);

  nlohmann::json results = nlohmann::json::array();
  int succeeded = 0;
  int failed = 0;
  const std::string allowed_str = JoinAllowedTypes(allowed_types_);

  for (const MultipartPart* file_part : file_parts) {
    const std::string original_filename = NormalizeUploadedFilename(*file_part);
    nlohmann::json item = {
        {"filename", JsonSafeString(original_filename, "batch_upload.filename")},
        {"success", false}
    };

    if (file_part->data.size() > max_image_size_mb_ * 1024 * 1024) {
      item["error"] = "ERR_PAYLOAD_TOO_LARGE";
      item["message"] = "图片大小超过限制（" + std::to_string(max_image_size_mb_) + "MB）";
      ++failed;
      results.push_back(item);
      continue;
    }

    const std::string ext = GetExtension(original_filename);
    if (!IsAllowedExtension(ext, allowed_types_)) {
      item["error"] = "ERR_UNSUPPORTED_TYPE";
      item["message"] = "不支持的图片格式，允许：" + allowed_str;
      ++failed;
      results.push_back(item);
      continue;
    }

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string safe_name = SanitizeFilename(original_filename);
    const std::string stored_name = std::to_string(user->id) + "_" +
        std::to_string(now_ms) + "_" + safe_name;
    const std::string storage_path = upload_dir + "/" + stored_name;

    std::ofstream ofs(storage_path, std::ios::binary);
    if (!ofs) {
      Logger::Error("ImageMaterialController::BatchUpload: cannot write " + storage_path);
      item["error"] = "ERR_INTERNAL";
      item["message"] = "文件保存失败";
      ++failed;
      results.push_back(item);
      continue;
    }
    ofs.write(file_part->data.data(),
              static_cast<std::streamsize>(file_part->data.size()));
    ofs.close();

    ImageMaterialService::ImageMaterial mat;
    std::string create_error;
    if (!image_material_service_->Create(user->id, stored_name, original_filename,
                                         storage_path, file_part->data.size(), mat, create_error)) {
      std::filesystem::remove(storage_path, ec);
      item["error"] = "ERR_INTERNAL";
      item["message"] = "数据库写入失败：" + create_error;
      ++failed;
      results.push_back(item);
      continue;
    }

    const std::string image_id = mat.id;
    auto svc = image_material_service_;
    if (thread_pool_) {
      thread_pool_->EnqueueDetached([image_id, svc]() mutable {
        svc->AnalyzeAndIndex(image_id);
      });
    }

    item["success"] = true;
    item["image"] = ImageToJson(mat);
    ++succeeded;
    results.push_back(item);
  }

  const int total = static_cast<int>(file_parts.size());
  const int status_code = (failed == 0) ? 201 : (succeeded == 0 ? 400 : 207);
  HttpResponse resp = HttpResponse::Json(status_code, {
      {"results", results},
      {"total", total},
      {"succeeded", succeeded},
      {"failed", failed}
  });
  if (status_code == 207) {
    resp.status_message = "Multi-Status";
  }
  return resp;
}

HttpResponse ImageMaterialController::List(const HttpRequest& request) {
  std::string error;
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(200, nlohmann::json{{"images", nlohmann::json::array()}});
  }

  std::string list_error;
  const auto images = image_material_service_->List(user->id, list_error);

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& m : images) {
    arr.push_back(ImageToJson(m));
  }
  return HttpResponse::Json(200, nlohmann::json{{"images", arr}});
}

HttpResponse ImageMaterialController::GetStatus(const HttpRequest& request) {
  std::string error;
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  const auto id_it = request.query_params.find("id");
  if (id_it == request.query_params.end() || id_it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 id 参数"));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_SERVICE_UNAVAILABLE", "服务未启用"));
  }

  ImageMaterialService::ImageMaterial mat;
  std::string get_error;
  if (!image_material_service_->Get(id_it->second, user->id, mat, get_error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND", "图片素材不存在"));
  }

  return HttpResponse::Json(200, nlohmann::json{{"image", ImageToJson(mat)}});
}

HttpResponse ImageMaterialController::GetFile(const HttpRequest& request) {
  std::string error;
  // Accept token via query param for browser <img> src usage
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    // Retry with token query param
    const auto tok_it = request.query_params.find("token");
    if (tok_it != request.query_params.end() && !tok_it->second.empty()) {
      error.clear();
      auto opt = auth_service_->GetUserFromToken(tok_it->second, error);
      if (opt) {
        user = std::make_shared<User>(*opt);
      }
    }
  }
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  const auto id_it = request.query_params.find("id");
  if (id_it == request.query_params.end() || id_it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 id 参数"));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_SERVICE_UNAVAILABLE", "服务未启用"));
  }

  ImageMaterialService::ImageMaterial mat;
  std::string get_error;
  if (!image_material_service_->Get(id_it->second, user->id, mat, get_error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND", "图片素材不存在"));
  }

  if (mat.storage_path.empty() || !std::filesystem::exists(mat.storage_path)) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND", "文件不存在"));
  }

  // Read file
  std::ifstream ifs(mat.storage_path, std::ios::binary);
  if (!ifs) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "文件读取失败"));
  }
  std::string data((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());

  const std::string ext = [&]() {
    auto dot = mat.filename.rfind('.');
    if (dot == std::string::npos) return std::string("jpg");
    std::string e = mat.filename.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
  }();

  const std::string mime = (ext == "png") ? "image/png"
                         : (ext == "gif") ? "image/gif"
                         : (ext == "webp") ? "image/webp"
                         : "image/jpeg";

  HttpResponse resp;
  resp.status_code = 200;
  resp.body = std::move(data);
  resp.headers["content-type"] = mime;
  resp.headers["cache-control"] = "max-age=3600";
  resp.headers["content-disposition"] =
      "inline; filename=\"" +
      JsonSafeString(LeafFilename(mat.original_filename), "download.original_filename") + "\"";
  return resp;
}

HttpResponse ImageMaterialController::Delete(const HttpRequest& request) {
  std::string error;
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  const auto id_it = request.query_params.find("id");
  if (id_it == request.query_params.end() || id_it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 id 参数"));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_SERVICE_UNAVAILABLE", "服务未启用"));
  }

  std::string del_error;
  if (!image_material_service_->Delete(id_it->second, user->id, del_error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND",
        del_error.empty() ? "图片素材不存在" : del_error));
  }

  return HttpResponse::Json(200, nlohmann::json{{"success", true}, {"message", "删除成功"}});
}

HttpResponse ImageMaterialController::BatchDelete(const HttpRequest& request) {
  std::string error;
  auto user = AuthenticateRequest(request, *auth_service_, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
        error.empty() ? "Unauthorized" : error));
  }

  if (!image_material_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_SERVICE_UNAVAILABLE", "服务未启用"));
  }

  if (request.body.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing request body"));
  }

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON body"));
  }

  if (!body.contains("ids") || !body["ids"].is_array() || body["ids"].empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "ids must be a non-empty array"));
  }

  const auto& ids_json = body["ids"];
  constexpr std::size_t kMaxBatch = 50;
  if (ids_json.size() > kMaxBatch) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "单次最多批量删除 50 条"));
  }

  nlohmann::json results = nlohmann::json::array();
  int success_count = 0;

  for (const auto& v : ids_json) {
    std::string image_id;
    if (v.is_string()) image_id = v.get<std::string>();
    else if (v.is_number()) image_id = std::to_string(v.get<std::int64_t>());

    nlohmann::json item;
    item["id"] = image_id;

    if (image_id.empty()) {
      item["status"] = "error";
      item["message"] = "invalid id";
      results.push_back(item);
      continue;
    }

    std::string del_error;
    if (!image_material_service_->Delete(image_id, user->id, del_error)) {
      item["status"] = "error";
      item["message"] = del_error.empty() ? "图片素材不存在" : del_error;
      results.push_back(item);
      continue;
    }

    item["status"] = "ok";
    item["message"] = "deleted";
    results.push_back(item);
    ++success_count;
  }

  const int total = static_cast<int>(ids_json.size());
  const int failed = total - success_count;
  const int status_code = (failed == 0) ? 200 : (success_count == 0 ? 400 : 207);
  return HttpResponse::Json(status_code, {
      {"success", success_count},
      {"failed", failed},
      {"total", total},
      {"results", results}
  });
}
