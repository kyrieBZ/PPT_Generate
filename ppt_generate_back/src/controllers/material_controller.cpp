#include "controllers/material_controller.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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
  j["id"]          = m.id;
  j["userId"]      = m.user_id;
  j["filename"]    = m.filename;
  j["fileType"]    = m.file_type;
  j["fileSize"]    = m.file_size;
  j["status"]      = m.status;
  j["createdAt"]   = m.created_at;
  j["updatedAt"]   = m.updated_at;
  j["storageType"] = m.storage_type.empty() ? "local" : m.storage_type;
  if (!m.fastdfs_url.empty()) {
    j["accessUrl"] = m.fastdfs_url;
  }
  if (!m.error_msg.empty()) {
    j["errorMsg"] = m.error_msg;
  }
  return j;
}

}  // namespace

MaterialController::MaterialController(std::shared_ptr<AuthService>    auth_service,
                                       std::shared_ptr<MaterialService> material_service,
                                       std::shared_ptr<ThreadPool>      thread_pool,
                                       std::string                      qwen_api_key,
                                       std::uint32_t                    qwen_timeout_sec,
                                       std::shared_ptr<AuditService>    audit_service,
                                       std::shared_ptr<KnowledgeRagService> knowledge_rag_service)
    : auth_service_(std::move(auth_service)),
      material_service_(std::move(material_service)),
      thread_pool_(std::move(thread_pool)),
      qwen_api_key_(std::move(qwen_api_key)),
      qwen_timeout_sec_(qwen_timeout_sec > 0 ? qwen_timeout_sec : 60),
      audit_service_(std::move(audit_service)),
      knowledge_rag_service_(std::move(knowledge_rag_service)) {}

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

HttpResponse MaterialController::BatchUpload(const HttpRequest& request) {
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

  // Collect all file parts
  std::vector<const MultipartPart*> file_parts;
  for (const auto& p : parts) {
    if (!p.filename.empty()) {
      file_parts.push_back(&p);
    }
  }

  if (file_parts.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "No files found in upload"));
  }

  constexpr int kMaxBatchFiles = 10;
  if (static_cast<int>(file_parts.size()) > kMaxBatchFiles) {
    return HttpResponse::Json(400, ErrorJson("ERR_BATCH_TOO_MANY",
        "单次最多上传 " + std::to_string(kMaxBatchFiles) + " 个文件"));
  }

  const std::uint64_t max_bytes = material_service_->config().max_file_size_mb * 1024 * 1024;
  const auto& allowed = material_service_->config().allowed_types;

  const std::string upload_dir =
      material_service_->config().upload_dir + "/" + std::to_string(user->id);
  {
    std::error_code ec;
    std::filesystem::create_directories(upload_dir, ec);
    if (ec) {
      Logger::Error("MaterialController::BatchUpload: cannot create upload dir: " + upload_dir);
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }
  }

  nlohmann::json results = nlohmann::json::array();
  int succeeded = 0, failed = 0;

  for (int idx = 0; idx < static_cast<int>(file_parts.size()); ++idx) {
    const auto* fp = file_parts[idx];
    nlohmann::json item;
    item["index"]    = idx;
    item["filename"] = fp->filename;

    // Type check
    const std::string ext = GetExtension(fp->filename);
    if (std::find(allowed.begin(), allowed.end(), ext) == allowed.end()) {
      item["success"] = false;
      item["error"]   = "ERR_MATERIAL_INVALID_TYPE";
      item["message"] = "不支持的文件类型，仅支持: pdf, docx, txt";
      ++failed;
      results.push_back(item);
      continue;
    }

    // Size check
    if (fp->data.empty()) {
      item["success"] = false;
      item["error"]   = "ERR_MATERIAL_EMPTY";
      item["message"] = "上传文件为空";
      ++failed;
      results.push_back(item);
      continue;
    }
    if (fp->data.size() > max_bytes) {
      item["success"] = false;
      item["error"]   = "ERR_MATERIAL_TOO_LARGE";
      item["message"] = "文件超过最大限制 " +
                        std::to_string(material_service_->config().max_file_size_mb) + "MB";
      ++failed;
      results.push_back(item);
      continue;
    }

    // Write to disk; use index suffix to avoid timestamp collision between files in same batch
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const std::string safe_name = SanitizeFilename(fp->filename);
    const std::string stored_filename =
        std::to_string(now_ms) + "_" + std::to_string(idx) + "_" + safe_name;
    const std::string file_path = upload_dir + "/" + stored_filename;

    {
      std::ofstream out(file_path, std::ios::binary);
      if (!out.is_open()) {
        Logger::Error("MaterialController::BatchUpload: cannot write file: " + file_path);
        item["success"] = false;
        item["error"]   = "ERR_INTERNAL";
        item["message"] = "文件写入失败";
        ++failed;
        results.push_back(item);
        continue;
      }
      out.write(fp->data.data(), static_cast<std::streamsize>(fp->data.size()));
      if (!out.good()) {
        Logger::Error("MaterialController::BatchUpload: write error: " + file_path);
        std::error_code ec;
        std::filesystem::remove(file_path, ec);
        item["success"] = false;
        item["error"]   = "ERR_INTERNAL";
        item["message"] = "文件写入中断";
        ++failed;
        results.push_back(item);
        continue;
      }
    }

    // Create DB record
    Material material;
    std::string create_err;
    if (!material_service_->CreateMaterial(user->id, fp->filename, ext,
                                           file_path, fp->data.size(),
                                           material, create_err)) {
      std::error_code ec;
      std::filesystem::remove(file_path, ec);
      Logger::Error("MaterialController::BatchUpload: CreateMaterial failed: " + create_err);
      item["success"] = false;
      item["error"]   = "ERR_INTERNAL";
      item["message"] = create_err.empty() ? "数据库写入失败" : create_err;
      ++failed;
      results.push_back(item);
      continue;
    }

    // Dispatch async extraction task per file
    const std::string material_id = material.id;
    auto svc = material_service_;
    thread_pool_->EnqueueDetached([svc, material_id]() {
      svc->RunExtraction(material_id);
    });

    item["success"]  = true;
    item["material"] = MaterialToJson(material);
    ++succeeded;
    results.push_back(item);
  }

  nlohmann::json payload = {
      {"results",   results},
      {"total",     static_cast<int>(file_parts.size())},
      {"succeeded", succeeded},
      {"failed",    failed}
  };
  // 207 Multi-Status: indicates mixed results
  HttpResponse resp = HttpResponse::Json(207, payload);
  resp.status_message = "Multi-Status";
  return resp;
}

HttpResponse MaterialController::BatchStatus(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  // Parse comma-separated ids from query param: ?ids=id1,id2,id3
  std::string ids_param;
  if (auto it = request.query_params.find("ids"); it != request.query_params.end()) {
    ids_param = it->second;
  }
  if (ids_param.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing ids parameter"));
  }

  // Split by comma
  std::vector<std::string> ids;
  {
    std::istringstream ss(ids_param);
    std::string token;
    while (std::getline(ss, token, ',')) {
      const auto trimmed = Trim(token);
      if (!trimmed.empty()) ids.push_back(trimmed);
    }
  }

  if (ids.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "No valid ids provided"));
  }

  constexpr int kMaxIds = 20;
  if (static_cast<int>(ids.size()) > kMaxIds) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST",
        "最多同时查询 " + std::to_string(kMaxIds) + " 条状态"));
  }

  auto arr = nlohmann::json::array();
  for (const auto& id : ids) {
    Material mat;
    std::string mat_err;
    if (material_service_->GetMaterial(id, user->id, mat, mat_err)) {
      nlohmann::json item = MaterialToJson(mat);
      arr.push_back(item);
    }
    // Silently skip ids that don't belong to this user or don't exist
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

HttpResponse MaterialController::BatchDelete(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
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
    std::string material_id;
    if (v.is_string()) material_id = v.get<std::string>();
    else if (v.is_number()) material_id = std::to_string(v.get<std::int64_t>());

    nlohmann::json item;
    item["id"] = material_id;

    if (material_id.empty()) {
      item["status"] = "error";
      item["message"] = "invalid id";
      results.push_back(item);
      continue;
    }

    std::string item_error;
    if (!material_service_->DeleteMaterial(material_id, user->id, item_error)) {
      item["status"] = "error";
      item["message"] = item_error.empty() ? "材料不存在" : item_error;
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

// ── 管理员专用接口 ──────────────────────────────────────────────────────────

namespace {
std::shared_ptr<User> AuthenticateAdminMaterial(
    const std::shared_ptr<AuthService>& auth_service,
    const HttpRequest& request,
    std::string& error) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    header = header.substr(7);
  }
  if (header.empty()) {
    if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
      header = it->second;
    }
  }
  if (header.empty()) { error = "Token not provided"; return nullptr; }
  auto user = auth_service->GetUserFromToken(header, error);
  if (!user) { error = error.empty() ? "Invalid token" : error; return nullptr; }
  if (!user->is_admin) { error = "Forbidden"; return nullptr; }
  return std::make_shared<User>(*user);
}

nlohmann::json AdminMaterialToJson(const Material& m) {
  nlohmann::json j = MaterialToJson(m);
  // Decode username from the encoded error_msg convention used in service layer
  if (m.error_msg.rfind("__username__:", 0) == 0) {
    j["username"] = m.error_msg.substr(13);
    j["errorMsg"] = nullptr;
  }
  return j;
}
}  // namespace

HttpResponse MaterialController::AdminGetContent(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  Material mat;
  if (!material_service_->AdminGetMaterial(material_id, mat, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_MATERIAL_NOT_FOUND", error.empty() ? "材料不存在" : error));
  }

  nlohmann::json payload;
  payload["id"]            = mat.id;
  payload["filename"]      = mat.filename;
  payload["fileType"]      = mat.file_type;
  payload["fileSize"]      = mat.file_size;
  payload["status"]        = mat.status;
  payload["userId"]        = mat.user_id;
  payload["createdAt"]     = mat.created_at;

  // 解析提取结果
  if (!mat.extract_result.empty()) {
    try {
      payload["extractResult"] = nlohmann::json::parse(mat.extract_result);
    } catch (...) {
      payload["extractResult"] = mat.extract_result;
    }
  } else {
    payload["extractResult"] = nullptr;
  }

  // 解析审核结论
  if (!mat.review_result.empty()) {
    try {
      payload["reviewResult"] = nlohmann::json::parse(mat.review_result);
    } catch (...) {
      payload["reviewResult"] = mat.review_result;
    }
  } else {
    payload["reviewResult"] = nullptr;
  }

  if (!mat.error_msg.empty()) {
    payload["errorMsg"] = mat.error_msg;
  }

  return HttpResponse::Json(200, payload);
}

HttpResponse MaterialController::AdminGetFile(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  Material mat;
  if (!material_service_->AdminGetMaterial(material_id, mat, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_MATERIAL_NOT_FOUND", error.empty() ? "材料不存在" : error));
  }

  // Determine MIME type from file_type
  std::string mime = "application/octet-stream";
  if (mat.file_type == "pdf") {
    mime = "application/pdf";
  } else if (mat.file_type == "docx") {
    mime = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
  } else if (mat.file_type == "txt") {
    mime = "text/plain; charset=utf-8";
  } else if (mat.file_type == "doc") {
    mime = "application/msword";
  }

  // 若已存储在 FastDFS，302 重定向到 FastDFS HTTP URL
  if (mat.storage_type == "fastdfs" && !mat.fastdfs_url.empty()) {
    HttpResponse resp;
    resp.status_code = 302;
    resp.status_message = "Found";
    resp.headers["location"] = mat.fastdfs_url;
    resp.headers["cache-control"] = "no-store";
    return resp;
  }

  // 本地文件读取
  std::ifstream file(mat.file_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    Logger::Error("AdminGetFile: cannot open file: " + mat.file_path);
    return HttpResponse::Json(404, ErrorJson("ERR_FILE_NOT_FOUND", "原始文件不存在或已被移动"));
  }

  const std::streamsize file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string content(static_cast<std::size_t>(file_size), '\0');
  if (!file.read(content.data(), file_size)) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "文件读取失败"));
  }

  HttpResponse resp;
  resp.status_code = 200;
  resp.status_message = "OK";
  resp.body = std::move(content);
  resp.headers["content-type"] = mime;
  // inline: 让浏览器直接展示（pdf/txt），而非强制下载
  resp.headers["content-disposition"] = "inline; filename=\"" + mat.filename + "\"";
  resp.headers["content-length"] = std::to_string(file_size);
  resp.headers["cache-control"] = "no-store";
  return resp;
}

HttpResponse MaterialController::AdminReview(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  MaterialService::ReviewResult review;
  if (!material_service_->AdminReviewMaterial(material_id, qwen_api_key_,
                                               qwen_timeout_sec_, review, error)) {
    Logger::Error(std::string("AdminReview failed for ") + material_id + ": " + error);
    return HttpResponse::Json(422, ErrorJson("ERR_REVIEW_FAILED", error.empty() ? "AI 审核失败" : error));
  }

  return HttpResponse::Json(200, {
    {"id",          material_id},
    {"result",      review.result},
    {"reason",      review.reason},
    {"reviewedAt",  review.reviewed_at}
  });
}

HttpResponse MaterialController::AdminList(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  MaterialService::AdminMaterialFilter filter;

  if (auto it = request.query_params.find("user_id"); it != request.query_params.end()) {
    try { filter.user_id = std::stoull(it->second); } catch (...) {}
  }
  if (auto it = request.query_params.find("status"); it != request.query_params.end()) {
    filter.status = it->second;
  }
  if (auto it = request.query_params.find("file_type"); it != request.query_params.end()) {
    filter.file_type = it->second;
  }
  if (auto it = request.query_params.find("page"); it != request.query_params.end()) {
    try { filter.page = std::stoi(it->second); } catch (...) {}
  }
  if (auto it = request.query_params.find("page_size"); it != request.query_params.end()) {
    try { filter.page_size = std::stoi(it->second); } catch (...) {}
  }

  int total = 0;
  auto items = material_service_->AdminListMaterials(filter, total, error);
  if (!error.empty()) {
    Logger::Error(std::string("AdminListMaterials failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "获取素材列表失败"));
  }

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& m : items) {
    arr.push_back(AdminMaterialToJson(m));
  }

  return HttpResponse::Json(200, {
    {"items", arr},
    {"total", total},
    {"page", filter.page},
    {"pageSize", filter.page_size}
  });
}

HttpResponse MaterialController::AdminStats(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  auto stats = material_service_->AdminGetStats(error);
  if (!error.empty()) {
    Logger::Error(std::string("AdminGetStats failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "获取统计数据失败"));
  }

  return HttpResponse::Json(200, {
    {"total",     stats.total},
    {"totalSize", stats.total_size},
    {"completed", stats.completed},
    {"pending",   stats.pending},
    {"failed",    stats.failed}
  });
}

HttpResponse MaterialController::AdminDelete(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing id"));
  }

  // 解析删除原因（可选，来自请求体 JSON）
  std::string delete_reason;
  if (!request.body.empty()) {
    try {
      auto body = nlohmann::json::parse(request.body);
      if (body.contains("reason") && body["reason"].is_string()) {
        delete_reason = body["reason"].get<std::string>();
      }
    } catch (...) {}
  }

  if (!material_service_->AdminDeleteMaterial(material_id, delete_reason, admin->username, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_MATERIAL_NOT_FOUND", error.empty() ? "材料不存在" : error));
  }

  if (audit_service_) {
    std::string detail = "{\"reason\":\"" + delete_reason + "\"}";
    audit_service_->Write(admin->id, admin->username,
                          "delete_material", "material", material_id,
                          detail, request.Header("x-forwarded-for").empty()
                                      ? request.Header("x-real-ip")
                                      : request.Header("x-forwarded-for"));
  }

  return HttpResponse::Json(200, {{"message", "删除成功"}, {"id", material_id}});
}

HttpResponse MaterialController::AdminBatchDelete(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdminMaterial(auth_service_, request, error);
  if (!admin) {
    if (error == "Forbidden") return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error));
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

  // 解析删除原因（可选）
  std::string delete_reason;
  if (body.contains("reason") && body["reason"].is_string()) {
    delete_reason = body["reason"].get<std::string>();
  }

  const auto& ids_json = body["ids"];
  constexpr std::size_t kMaxBatch = 100;
  if (ids_json.size() > kMaxBatch) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "单次最多批量删除 100 条"));
  }

  nlohmann::json results = nlohmann::json::array();
  int success_count = 0;
  for (const auto& v : ids_json) {
    std::string mid;
    if (v.is_string()) mid = v.get<std::string>();
    else if (v.is_number()) mid = std::to_string(v.get<std::int64_t>());

    nlohmann::json item;
    item["id"] = mid;
    if (mid.empty()) {
      item["status"] = "error"; item["message"] = "invalid id";
      results.push_back(item); continue;
    }
    std::string item_error;
    if (!material_service_->AdminDeleteMaterial(mid, delete_reason, admin->username, item_error)) {
      item["status"] = "error"; item["message"] = item_error.empty() ? "材料不存在" : item_error;
    } else {
      item["status"] = "ok"; item["message"] = "deleted";
      ++success_count;
    }
    results.push_back(item);
  }

  const int total = static_cast<int>(ids_json.size());
  const int failed = total - success_count;
  const int sc = (failed == 0) ? 200 : (success_count == 0 ? 400 : 207);

  if (audit_service_ && success_count > 0) {
    std::string detail = std::string("{\"reason\":\"") + delete_reason +
                         "\",\"success\":" + std::to_string(success_count) + "}";
    audit_service_->Write(admin->id, admin->username,
                          "batch_delete_material", "material", "batch",
                          detail, request.Header("x-forwarded-for").empty()
                                      ? request.Header("x-real-ip")
                                      : request.Header("x-forwarded-for"));
  }

  return HttpResponse::Json(sc, {
    {"success", success_count}, {"failed", failed}, {"total", total}, {"results", results}
  });
}

HttpResponse MaterialController::GetDeletionNotices(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  auto notices = material_service_->GetDeletionNotices(user->id, error);
  if (!error.empty()) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  auto arr = nlohmann::json::array();
  for (const auto& n : notices) {
    arr.push_back({
      {"id",           n.id},
      {"filename",     n.filename},
      {"fileType",     n.file_type},
      {"fileSize",     n.file_size},
      {"deleteReason", n.delete_reason},
      {"deletedBy",    n.deleted_by},
      {"createdAt",    n.created_at}
    });
  }
  return HttpResponse::Json(200, {{"notices", arr}});
}

HttpResponse MaterialController::MarkNoticesRead(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::vector<std::uint64_t> ids;
  if (!request.body.empty()) {
    try {
      auto body = nlohmann::json::parse(request.body);
      if (body.contains("ids") && body["ids"].is_array()) {
        for (const auto& v : body["ids"]) {
          if (v.is_number_unsigned()) ids.push_back(v.get<std::uint64_t>());
          else if (v.is_number()) ids.push_back(static_cast<std::uint64_t>(v.get<std::int64_t>()));
        }
      }
    } catch (...) {}
  }

  if (!material_service_->MarkNoticesRead(user->id, ids, error)) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }
  return HttpResponse::Json(200, {{"message", "已标记为已读"}});
}

// ──────────────────────────────────────────────────────────────────────────────
// RAG 知识库接口

HttpResponse MaterialController::RagIndex(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
                                             error.empty() ? "Unauthorized" : error));
  }

  std::string material_id;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    material_id = it->second;
  }
  if (material_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_MISSING_PARAM", "缺少 id 参数"));
  }

  if (!knowledge_rag_service_ || !knowledge_rag_service_->IsAvailable()) {
    return HttpResponse::Json(503, ErrorJson("ERR_RAG_UNAVAILABLE",
                                             "RAG 知识库服务不可用（Qdrant 或 Qwen Embedding 未启用）"));
  }

  // 权限验证：确认素材属于该用户
  Material mat;
  std::string mat_error;
  if (!material_service_->GetMaterial(material_id, user->id, mat, mat_error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND", "素材不存在或无权访问"));
  }
  if (mat.status != "completed" || mat.extract_result.empty()) {
    return HttpResponse::Json(422, ErrorJson("ERR_NOT_READY",
                                             "素材尚未提取完成，无法建立知识库索引"));
  }

  const int chunks = material_service_->IndexMaterialToRag(material_id);
  if (chunks < 0) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "RAG 索引写入失败"));
  }

  return HttpResponse::Json(200, {
    {"material_id", material_id},
    {"chunks",      chunks},
    {"message",     "知识库索引成功，共 " + std::to_string(chunks) + " 个知识块"}
  });
}

HttpResponse MaterialController::RagStatus(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED",
                                             error.empty() ? "Unauthorized" : error));
  }

  const bool available = knowledge_rag_service_ && knowledge_rag_service_->IsAvailable();
  int total_chunks = 0;
  if (available) {
    std::string count_err;
    total_chunks = knowledge_rag_service_->CountUserChunks(user->id, count_err);
    if (total_chunks < 0) total_chunks = 0;
  }

  return HttpResponse::Json(200, {
    {"available",    available},
    {"total_chunks", total_chunks},
    {"message",      available
        ? "知识库服务可用，您共有 " + std::to_string(total_chunks) + " 个知识块"
        : "知识库服务未启用（需配置 Qdrant + Qwen AI 检索）"}
  });
}
