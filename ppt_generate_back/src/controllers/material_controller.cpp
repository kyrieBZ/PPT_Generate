#include "controllers/material_controller.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"

namespace {

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

/** Extract boundary from Content-Type header, e.g. "multipart/form-data; boundary=----WebKitFormBoundary..." */
std::string ExtractBoundary(const std::string& content_type) {
  const std::string key = "boundary=";
  const auto pos = content_type.find(key);
  if (pos == std::string::npos) return {};
  std::string boundary = content_type.substr(pos + key.size());
  // Strip quotes if present
  if (!boundary.empty() && boundary.front() == '"') {
    boundary = boundary.substr(1);
    const auto q = boundary.find('"');
    if (q != std::string::npos) boundary.resize(q);
  }
  // Strip trailing params
  const auto semi = boundary.find(';');
  if (semi != std::string::npos) boundary.resize(semi);
  return Trim(boundary);
}

struct MultipartPart {
  std::string name;
  std::string filename;
  std::string content_type;
  std::string data;
};

/** Parse multipart/form-data body. Returns list of parts. */
std::vector<MultipartPart> ParseMultipart(const std::string& body, const std::string& boundary) {
  std::vector<MultipartPart> parts;
  if (boundary.empty()) return parts;

  const std::string delimiter = "--" + boundary;
  const std::string final_delimiter = "--" + boundary + "--";

  std::size_t pos = 0;
  while (pos < body.size()) {
    // Find next delimiter
    const auto delim_pos = body.find(delimiter, pos);
    if (delim_pos == std::string::npos) break;

    // Skip past delimiter + CRLF
    std::size_t after_delim = delim_pos + delimiter.size();
    if (after_delim + 1 < body.size() && body[after_delim] == '-' && body[after_delim + 1] == '-') {
      break;  // final delimiter
    }
    if (after_delim < body.size() && body[after_delim] == '\r') after_delim++;
    if (after_delim < body.size() && body[after_delim] == '\n') after_delim++;

    // Find end of headers (blank line)
    const auto header_end = body.find("\r\n\r\n", after_delim);
    if (header_end == std::string::npos) break;

    const std::string headers_section = body.substr(after_delim, header_end - after_delim);
    const std::size_t data_start = header_end + 4;

    // Find next delimiter to determine data end
    const auto next_delim = body.find("\r\n" + delimiter, data_start);
    const std::size_t data_end = (next_delim == std::string::npos) ? body.size() : next_delim;

    MultipartPart part;
    part.data = body.substr(data_start, data_end - data_start);

    // Parse headers
    std::istringstream hstream(headers_section);
    std::string hline;
    while (std::getline(hstream, hline)) {
      if (!hline.empty() && hline.back() == '\r') hline.pop_back();
      const auto colon = hline.find(':');
      if (colon == std::string::npos) continue;
      const auto hkey = ToLower(Trim(hline.substr(0, colon)));
      const auto hval = Trim(hline.substr(colon + 1));

      if (hkey == "content-disposition") {
        // Extract name and filename
        std::istringstream dstream(hval);
        std::string token;
        while (std::getline(dstream, token, ';')) {
          token = Trim(token);
          if (token.rfind("name=", 0) == 0) {
            part.name = Trim(token.substr(5));
          } else if (token.rfind("filename=", 0) == 0) {
            part.filename = Trim(token.substr(9));
          }
        }
      } else if (hkey == "content-type") {
        part.content_type = hval;
      }
    }

    if (!part.name.empty()) {
      parts.push_back(std::move(part));
    }
    pos = data_end;
  }
  return parts;
}

/** Sanitize filename: keep alphanumeric, dash, underscore, dot. */
std::string SanitizeFilename(const std::string& name) {
  std::string result;
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
      result.push_back(static_cast<char>(c));
    } else {
      result.push_back('_');
    }
  }
  if (result.empty()) result = "upload";
  if (result.size() > 200) result.resize(200);
  return result;
}

/** Get file extension (lowercase, without dot). */
std::string GetExtension(const std::string& filename) {
  const auto dot = filename.rfind('.');
  if (dot == std::string::npos) return {};
  std::string ext = filename.substr(dot + 1);
  return ToLower(ext);
}

nlohmann::json MaterialToJson(const Material& m) {
  nlohmann::json j;
  j["id"]        = m.id;
  j["userId"]    = m.user_id;
  j["filename"]  = m.filename;
  j["fileType"]  = m.file_type;
  j["fileSize"]  = m.file_size;
  j["status"]    = m.status;
  j["createdAt"] = m.created_at;
  j["updatedAt"] = m.updated_at;
  if (!m.error_msg.empty()) {
    j["errorMsg"] = m.error_msg;
  }
  return j;
}

}  // namespace

MaterialController::MaterialController(std::shared_ptr<AuthService> auth_service,
                                       std::shared_ptr<MaterialService> material_service,
                                       std::shared_ptr<ThreadPool> thread_pool)
    : auth_service_(std::move(auth_service)),
      material_service_(std::move(material_service)),
      thread_pool_(std::move(thread_pool)) {}

std::shared_ptr<User> MaterialController::Authenticate(const HttpRequest& request,
                                                        std::string& error) const {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    header = header.substr(7);
  }
  if (header.empty()) {
    error = "Token not provided";
    return nullptr;
  }
  auto user = auth_service_->GetUserFromToken(header, error);
  if (!user) {
    error = error.empty() ? "Invalid token" : error;
    return nullptr;
  }
  return std::make_shared<User>(*user);
}

HttpResponse MaterialController::Upload(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  const auto content_type = request.Header("content-type");
  if (content_type.find("multipart/form-data") == std::string::npos) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Expected multipart/form-data"));
  }

  const std::string boundary = ExtractBoundary(content_type);
  if (boundary.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing multipart boundary"));
  }

  const auto parts = ParseMultipart(request.body, boundary);
  if (parts.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "No parts found in multipart body"));
  }

  // Find the file part
  const MultipartPart* file_part = nullptr;
  for (const auto& p : parts) {
    if (!p.filename.empty()) {
      file_part = &p;
      break;
    }
  }
  if (!file_part) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "No file found in upload"));
  }

  const std::string ext = GetExtension(file_part->filename);
  const auto& allowed = material_service_->config().allowed_types;
  if (std::find(allowed.begin(), allowed.end(), ext) == allowed.end()) {
    return HttpResponse::Json(400, ErrorJson("ERR_MATERIAL_INVALID_TYPE",
                                             "不支持的文件类型，仅支持: pdf, docx, txt"));
  }

  const std::uint64_t max_bytes = material_service_->config().max_file_size_mb * 1024 * 1024;
  if (file_part->data.size() > max_bytes) {
    return HttpResponse::Json(400, ErrorJson("ERR_MATERIAL_TOO_LARGE",
                                             "文件超过最大限制 " +
                                             std::to_string(material_service_->config().max_file_size_mb) + "MB"));
  }

  if (file_part->data.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_MATERIAL_EMPTY", "上传文件为空"));
  }

  // Save file to disk
  const std::string upload_dir = material_service_->config().upload_dir + "/" + std::to_string(user->id);
  std::error_code ec;
  std::filesystem::create_directories(upload_dir, ec);
  if (ec) {
    Logger::Error("MaterialController: cannot create upload dir: " + upload_dir);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  // Generate a unique filename using timestamp + original name
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  const std::string safe_name = SanitizeFilename(file_part->filename);
  const std::string stored_filename = std::to_string(now_ms) + "_" + safe_name;
  const std::string file_path = upload_dir + "/" + stored_filename;

  {
    std::ofstream out(file_path, std::ios::binary);
    if (!out.is_open()) {
      Logger::Error("MaterialController: cannot write file: " + file_path);
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }
    out.write(file_part->data.data(), static_cast<std::streamsize>(file_part->data.size()));
    if (!out.good()) {
      Logger::Error("MaterialController: write error: " + file_path);
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }
  }

  // Create DB record
  Material material;
  if (!material_service_->CreateMaterial(user->id, file_part->filename, ext,
                                          file_path, file_part->data.size(),
                                          material, error)) {
    std::filesystem::remove(file_path, ec);
    Logger::Error("MaterialController: CreateMaterial failed: " + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  // Async extraction
  const std::string material_id = material.id;
  auto svc = material_service_;
  thread_pool_->EnqueueDetached([svc, material_id]() {
    svc->RunExtraction(material_id);
  });

  nlohmann::json payload = {{"material", MaterialToJson(material)}};
  HttpResponse resp = HttpResponse::Json(202, payload);
  resp.status_message = "Accepted";
  return resp;
}

HttpResponse MaterialController::GetStatus(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  Material material;
  if (!material_service_->GetMaterial(material_id, user->id, material, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_MATERIAL_NOT_FOUND", "材料不存在"));
  }

  return HttpResponse::Json(200, {{"material", MaterialToJson(material)}});
}

HttpResponse MaterialController::GetResult(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  Material material;
  if (!material_service_->GetMaterial(material_id, user->id, material, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_MATERIAL_NOT_FOUND", "材料不存在"));
  }

  nlohmann::json j = MaterialToJson(material);
  if (!material.extract_result.empty()) {
    try {
      j["extractResult"] = nlohmann::json::parse(material.extract_result);
    } catch (...) {
      j["extractResult"] = material.extract_result;
    }
  } else {
    j["extractResult"] = nullptr;
  }

  return HttpResponse::Json(200, {{"material", j}});
}

HttpResponse MaterialController::SaveResult(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("extractResult")) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing extractResult field"));
    }
    const std::string extract_json = body["extractResult"].dump();
    if (!material_service_->SaveExtractResult(material_id, user->id, extract_json, error)) {
      return HttpResponse::Json(400, ErrorJson("ERR_MATERIAL_SAVE_FAILED", error.empty() ? "保存失败" : error));
    }
    return HttpResponse::Json(200, {{"message", "保存成功"}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("MaterialController::SaveResult parse error: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse MaterialController::List(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  auto materials = material_service_->ListMaterials(user->id, error);
  auto arr = nlohmann::json::array();
  for (const auto& m : materials) {
    arr.push_back(MaterialToJson(m));
  }
  return HttpResponse::Json(200, {{"materials", arr}});
}

HttpResponse MaterialController::Delete(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  if (!material_service_->DeleteMaterial(material_id, user->id, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_MATERIAL_NOT_FOUND", error.empty() ? "材料不存在" : error));
  }

  return HttpResponse::Json(200, {{"message", "删除成功"}});
}
