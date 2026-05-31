#include "services/image_material_service.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <curl/curl.h>
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "logger.h"

namespace {

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* out) {
  const size_t total = size * nmemb;
  out->append(static_cast<char*>(contents), total);
  return total;
}

std::uint64_t NowSeconds() {
  return static_cast<std::uint64_t>(
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string GenerateUuid() {
  std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
  std::uniform_int_distribution<uint64_t> dist;
  char buf[37];
  uint64_t a = dist(rng), b = dist(rng);
  snprintf(buf, sizeof(buf),
           "%08x-%04x-%04x-%04x-%012llx",
           static_cast<uint32_t>(a >> 32),
           static_cast<uint32_t>((a >> 16) & 0xFFFF),
           static_cast<uint32_t>(a & 0x0FFF) | 0x4000,
           static_cast<uint32_t>((b >> 48) & 0x3FFF) | 0x8000,
           static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
  return buf;
}

std::uint64_t StrToUint64Hash(const std::string& s) {
  constexpr uint64_t kBasis = 14695981039346656037ULL;
  constexpr uint64_t kPrime = 1099511628211ULL;
  uint64_t h = kBasis;
  for (unsigned char c : s) { h ^= c; h *= kPrime; }
  return h;
}

bool IsUtf8ContinuationByte(unsigned char c) {
  return (c & 0xC0) == 0x80;
}

bool NextUtf8SequenceLength(const std::string& input, std::size_t index, std::size_t& length) {
  if (index >= input.size()) return false;

  const unsigned char lead = static_cast<unsigned char>(input[index]);
  if (lead <= 0x7F) {
    length = 1;
    return true;
  }
  if (lead >= 0xC2 && lead <= 0xDF) {
    if (index + 1 >= input.size()) return false;
    if (!IsUtf8ContinuationByte(static_cast<unsigned char>(input[index + 1]))) return false;
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
                                        : IsUtf8ContinuationByte(c1);
    if (!valid_second || !IsUtf8ContinuationByte(c2)) return false;
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
                                        : IsUtf8ContinuationByte(c1);
    if (!valid_second || !IsUtf8ContinuationByte(c2) || !IsUtf8ContinuationByte(c3)) return false;
    length = 4;
    return true;
  }
  return false;
}

bool IsValidUtf8(const std::string& input) {
  for (std::size_t i = 0; i < input.size();) {
    std::size_t seq_len = 0;
    if (!NextUtf8SequenceLength(input, i, seq_len)) return false;
    i += seq_len;
  }
  return true;
}

std::string Utf8SafeTruncate(const std::string& input, std::size_t max_bytes) {
  if (input.size() <= max_bytes) return input;

  std::size_t end = 0;
  for (std::size_t i = 0; i < input.size() && i < max_bytes;) {
    std::size_t seq_len = 0;
    if (!NextUtf8SequenceLength(input, i, seq_len)) {
      Logger::Warn("ImageMaterialService: invalid UTF-8 detected before truncation, keeping valid prefix only");
      break;
    }
    if (i + seq_len > max_bytes) break;
    end = i + seq_len;
    i += seq_len;
  }
  return input.substr(0, end);
}

std::string FileToBase64(const std::string& path, std::string& error) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    error = "Cannot open file: " + path;
    return {};
  }
  std::string data((std::istreambuf_iterator<char>(ifs)),
                    std::istreambuf_iterator<char>());

  static const char* kChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t b = (static_cast<uint8_t>(data[i]) << 16) |
                 (i + 1 < data.size() ? static_cast<uint8_t>(data[i + 1]) << 8 : 0) |
                 (i + 2 < data.size() ? static_cast<uint8_t>(data[i + 2]) : 0);
    out += kChars[(b >> 18) & 63];
    out += kChars[(b >> 12) & 63];
    out += (i + 1 < data.size()) ? kChars[(b >> 6) & 63] : '=';
    out += (i + 2 < data.size()) ? kChars[b & 63] : '=';
  }
  return out;
}

std::string GetFileExtension(const std::string& path) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos) return "jpg";
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

std::string GetMimeType(const std::string& ext) {
  if (ext == "png") return "image/png";
  if (ext == "gif") return "image/gif";
  if (ext == "webp") return "image/webp";
  return "image/jpeg";
}

ImageMaterialService::ImageMaterial RowToMaterial(MYSQL_ROW row, unsigned long* lengths) {
  ImageMaterialService::ImageMaterial m;
  if (!row) return m;
  int i = 0;
  m.id               = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.user_id          = row[i] ? std::stoull(std::string(row[i], lengths[i])) : 0; i++;
  m.filename         = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.original_filename= row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.storage_path     = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.description      = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.tags             = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.status           = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.error_msg        = row[i] ? std::string(row[i], lengths[i]) : ""; i++;
  m.file_size        = row[i] ? std::stoull(std::string(row[i], lengths[i])) : 0; i++;
  m.created_at       = row[i] ? std::stoull(std::string(row[i], lengths[i])) : 0; i++;
  m.updated_at       = row[i] ? std::stoull(std::string(row[i], lengths[i])) : 0;
  return m;
}

}  // namespace

ImageMaterialService::ImageMaterialService(std::shared_ptr<MySQLConnectionPool> pool,
                                           std::string upload_dir,
                                           std::shared_ptr<QwenClient> qwen_client,
                                           std::string qdrant_base_url,
                                           int vector_dim)
    : pool_(std::move(pool)),
      upload_dir_(std::move(upload_dir)),
      qwen_client_(std::move(qwen_client)),
      qdrant_base_url_(std::move(qdrant_base_url)),
      vector_dim_(vector_dim) {}

bool ImageMaterialService::IsAvailable() const {
  return qwen_client_ && qwen_client_->IsEnabled() && !qdrant_base_url_.empty();
}

bool ImageMaterialService::EnsureCollection(std::string& error) {
  if (!IsAvailable()) {
    error = "ImageMaterialService not available";
    return false;
  }
  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName);
  DoQdrantRequest("GET", path, "", code);
  if (code == 200) return true;

  nlohmann::json body = {
    {"vectors", {{"size", vector_dim_}, {"distance", "Cosine"}}}
  };
  const std::string resp = DoQdrantRequest("PUT", path, body.dump(), code);
  if (code == 200 || code == 201) return true;
  error = "EnsureCollection failed, HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

bool ImageMaterialService::Create(std::uint64_t user_id,
                                   const std::string& filename,
                                   const std::string& original_name,
                                   const std::string& storage_path,
                                   std::uint64_t file_size,
                                   ImageMaterial& out,
                                   std::string& error) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    error = "DB connection unavailable";
    return false;
  }

  const std::string id = GenerateUuid();
  const auto now = NowSeconds();

  const std::string sql =
      "INSERT INTO image_materials "
      "(id, user_id, filename, original_filename, storage_path, description, tags, "
      " status, error_msg, file_size, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, ?, '', '', 'pending', '', ?, ?, ?)";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "mysql_stmt_init failed"; return false; }

  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }

  MYSQL_BIND bind[8];
  memset(bind, 0, sizeof(bind));

  unsigned long id_len = id.size();
  unsigned long fn_len = filename.size();
  unsigned long on_len = original_name.size();
  unsigned long sp_len = storage_path.size();

  bind[0].buffer_type = MYSQL_TYPE_STRING;
  bind[0].buffer = const_cast<char*>(id.data());
  bind[0].buffer_length = id.size();
  bind[0].length = &id_len;

  bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
  bind[1].buffer = &user_id;
  bind[1].is_unsigned = true;

  bind[2].buffer_type = MYSQL_TYPE_STRING;
  bind[2].buffer = const_cast<char*>(filename.data());
  bind[2].buffer_length = filename.size();
  bind[2].length = &fn_len;

  bind[3].buffer_type = MYSQL_TYPE_STRING;
  bind[3].buffer = const_cast<char*>(original_name.data());
  bind[3].buffer_length = original_name.size();
  bind[3].length = &on_len;

  bind[4].buffer_type = MYSQL_TYPE_STRING;
  bind[4].buffer = const_cast<char*>(storage_path.data());
  bind[4].buffer_length = storage_path.size();
  bind[4].length = &sp_len;

  bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
  bind[5].buffer = &file_size;
  bind[5].is_unsigned = true;

  uint64_t created_copy = now, updated_copy = now;
  bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
  bind[6].buffer = &created_copy;
  bind[6].is_unsigned = true;

  bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
  bind[7].buffer = &updated_copy;
  bind[7].is_unsigned = true;

  if (mysql_stmt_bind_param(stmt, bind) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }
  mysql_stmt_close(stmt);

  out.id = id;
  out.user_id = user_id;
  out.filename = filename;
  out.original_filename = original_name;
  out.storage_path = storage_path;
  out.file_size = file_size;
  out.status = "pending";
  out.created_at = now;
  out.updated_at = now;
  return true;
}

void ImageMaterialService::AnalyzeAndIndex(const std::string& image_id) {
  // Get record
  std::string error;
  ImageMaterial mat;
  {
    auto conn_guard = pool_->GetConnection();
    MYSQL* conn = conn_guard.Get();
    if (!conn) {
      Logger::Warn("ImageMaterialService::AnalyzeAndIndex: no DB connection for " + image_id);
      return;
    }
    const std::string sql =
        "SELECT id, user_id, filename, original_filename, storage_path, "
        "description, tags, status, error_msg, file_size, created_at, updated_at "
        "FROM image_materials WHERE id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return;
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
      mysql_stmt_close(stmt);
      return;
    }
    MYSQL_BIND param[1];
    memset(param, 0, sizeof(param));
    unsigned long id_len = image_id.size();
    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].buffer = const_cast<char*>(image_id.data());
    param[0].buffer_length = image_id.size();
    param[0].length = &id_len;
    if (mysql_stmt_bind_param(stmt, param) != 0 ||
        mysql_stmt_execute(stmt) != 0 ||
        mysql_stmt_store_result(stmt) != 0) {
      mysql_stmt_close(stmt);
      return;
    }
    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (!meta) { mysql_stmt_close(stmt); return; }
    const int num_fields = mysql_num_fields(meta);
    mysql_free_result(meta);

    std::vector<std::string> bufs(num_fields);
    std::vector<unsigned long> lens(num_fields, 0);
    std::unique_ptr<bool[]> nulls(new bool[num_fields]());
    std::vector<MYSQL_BIND> res_bind(num_fields);
    memset(res_bind.data(), 0, sizeof(MYSQL_BIND) * num_fields);
    for (int i = 0; i < num_fields; ++i) {
      bufs[i].resize(4096);
      res_bind[i].buffer_type = MYSQL_TYPE_STRING;
      res_bind[i].buffer = &bufs[i][0];
      res_bind[i].buffer_length = 4096;
      res_bind[i].length = &lens[i];
      res_bind[i].is_null = &nulls[i];
    }
    mysql_stmt_bind_result(stmt, res_bind.data());
    if (mysql_stmt_fetch(stmt) == 0) {
      int idx = 0;
      mat.id = lens[idx] ? std::string(&bufs[idx][0], lens[idx]) : ""; idx++;
      mat.user_id = lens[idx] ? std::stoull(std::string(&bufs[idx][0], lens[idx])) : 0; idx++;
      mat.filename = lens[idx] ? std::string(&bufs[idx][0], lens[idx]) : ""; idx++;
      mat.original_filename = lens[idx] ? std::string(&bufs[idx][0], lens[idx]) : ""; idx++;
      mat.storage_path = lens[idx] ? std::string(&bufs[idx][0], lens[idx]) : ""; idx++;
      idx += 4;  // skip description, tags, status, error_msg
      mat.file_size = lens[idx] ? std::stoull(std::string(&bufs[idx][0], lens[idx])) : 0;
    }
    mysql_stmt_close(stmt);
  }

  if (mat.id.empty() || mat.storage_path.empty()) {
    Logger::Warn("ImageMaterialService::AnalyzeAndIndex: record not found for " + image_id);
    return;
  }

  UpdateStatus(image_id, "indexing", "", "");

  // Read image and convert to base64
  std::string b64_err;
  const std::string b64 = FileToBase64(mat.storage_path, b64_err);
  if (b64.empty()) {
    Logger::Warn("ImageMaterialService::AnalyzeAndIndex: " + b64_err);
    UpdateStatus(image_id, "failed", "", b64_err);
    return;
  }

  const std::string ext = GetFileExtension(mat.storage_path);
  const std::string mime = GetMimeType(ext);
  const std::string data_uri = "data:" + mime + ";base64," + b64;

  // Call Qwen-VL to analyze when available; otherwise fall back to filename.
  std::string description = mat.original_filename.empty() ? mat.filename : mat.original_filename;
  if (qwen_client_ && qwen_client_->IsEnabled()) {
    std::string analyze_error;
    const std::string hint = "请简洁描述这张图片的主要内容、场景、主题和视觉特征，用于语义检索。要求：中文，100字以内。";
    std::string analyzed_description;
    if (qwen_client_->AnalyzeImages({data_uri}, hint, analyzed_description, analyze_error)) {
      if (!analyzed_description.empty()) {
        description = std::move(analyzed_description);
      }
    } else {
      Logger::Warn("ImageMaterialService::AnalyzeAndIndex: Qwen-VL failed for " +
                   image_id + ": " + analyze_error);
    }
  }

  // Trim description without cutting a UTF-8 character in half.
  if (description.size() > 2000) {
    const auto truncated = Utf8SafeTruncate(description, 2000);
    if (truncated.size() != description.size()) {
      Logger::Info("ImageMaterialService::AnalyzeAndIndex: truncated description from " +
                   std::to_string(description.size()) + " to " +
                   std::to_string(truncated.size()) + " bytes for " + image_id);
    }
    description = truncated;
  }

  if (!IsValidUtf8(description)) {
    Logger::Warn("ImageMaterialService::AnalyzeAndIndex: description still contains invalid UTF-8 for " +
                 image_id + ", fallback to filename");
    description = mat.original_filename.empty() ? mat.filename : mat.original_filename;
    description = Utf8SafeTruncate(description, 2000);
  }

  // Qdrant 未启用时，图片依然应可用，只是不参与语义检索。
  if (qdrant_base_url_.empty()) {
    UpdateStatus(image_id, "ready", description, "");
    Logger::Info("ImageMaterialService::AnalyzeAndIndex: ready without Qdrant for " + image_id);
    return;
  }

  // Index to Qdrant
  std::string idx_error;
  if (!IndexToQdrant(image_id, mat.user_id, description, mat.filename, mat.storage_path, idx_error)) {
    Logger::Warn("ImageMaterialService::AnalyzeAndIndex: Qdrant index failed for " +
                 image_id + ": " + idx_error);
    UpdateStatus(image_id, "failed", description, idx_error);
    return;
  }

  UpdateStatus(image_id, "ready", description, "");
  Logger::Info("ImageMaterialService::AnalyzeAndIndex: indexed " + image_id);
}

std::vector<ImageMaterialService::SearchResult> ImageMaterialService::Search(
    const std::string& query,
    std::uint64_t user_id,
    const std::vector<std::string>& image_ids,
    int top_k,
    double score_threshold) const {
  if (!IsAvailable() || query.empty()) return {};

  const auto query_vec = qwen_client_->GetEmbedding(query);
  if (query_vec.empty()) {
    Logger::Warn("ImageMaterialService::Search: embedding failed for query");
    return {};
  }

  nlohmann::json vec_arr = nlohmann::json::array();
  for (float v : query_vec) vec_arr.push_back(v);

  nlohmann::json must_clauses = nlohmann::json::array();
  must_clauses.push_back({{"key", "user_id"}, {"match", {{"value", user_id}}}});
  must_clauses.push_back({{"key", "status"}, {"match", {{"value", "ready"}}}});

  if (!image_ids.empty()) {
    nlohmann::json any_values = nlohmann::json::array();
    for (const auto& id : image_ids) any_values.push_back(id);
    must_clauses.push_back({{"key", "image_id"}, {"match", {{"any", any_values}}}});
  }

  nlohmann::json body = {
    {"vector", vec_arr},
    {"limit", top_k},
    {"score_threshold", score_threshold},
    {"with_payload", true},
    {"filter", {{"must", must_clauses}}}
  };

  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points/search";
  const std::string resp = DoQdrantRequest("POST", path, body.dump(), code);
  if (code != 200) {
    Logger::Warn("ImageMaterialService::Search: HTTP " + std::to_string(code));
    return {};
  }

  std::vector<SearchResult> results;
  try {
    auto j = nlohmann::json::parse(resp);
    for (const auto& item : j.at("result")) {
      SearchResult sr;
      sr.score = item.value("score", 0.0);
      const auto& payload = item.at("payload");
      sr.image_id     = payload.value("image_id", "");
      sr.storage_path = payload.value("storage_path", "");
      sr.filename     = payload.value("filename", "");
      sr.description  = payload.value("description", "");
      if (!sr.storage_path.empty() &&
          std::filesystem::exists(sr.storage_path)) {
        results.push_back(std::move(sr));
      }
    }
  } catch (const std::exception& ex) {
    Logger::Warn(std::string("ImageMaterialService::Search parse error: ") + ex.what());
  }
  return results;
}

bool ImageMaterialService::Get(const std::string& image_id,
                                std::uint64_t user_id,
                                ImageMaterial& out,
                                std::string& error) const {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "DB connection unavailable"; return false; }

  const std::string sql =
      "SELECT id, user_id, filename, original_filename, storage_path, "
      "description, tags, status, error_msg, file_size, created_at, updated_at "
      "FROM image_materials WHERE id=? AND user_id=?";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "stmt init failed"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }

  MYSQL_BIND params[2];
  memset(params, 0, sizeof(params));
  unsigned long id_len = image_id.size();
  params[0].buffer_type = MYSQL_TYPE_STRING;
  params[0].buffer = const_cast<char*>(image_id.data());
  params[0].buffer_length = image_id.size();
  params[0].length = &id_len;
  params[1].buffer_type = MYSQL_TYPE_LONGLONG;
  params[1].buffer = const_cast<void*>(static_cast<const void*>(&user_id));
  params[1].is_unsigned = true;

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0 ||
      mysql_stmt_store_result(stmt) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }

  const int kNumFields = 12;
  std::vector<std::string> bufs(kNumFields);
  std::vector<unsigned long> lens(kNumFields, 0);
  std::unique_ptr<bool[]> nulls(new bool[kNumFields]());
  std::vector<MYSQL_BIND> res_bind(kNumFields);
  memset(res_bind.data(), 0, sizeof(MYSQL_BIND) * kNumFields);
  for (int i = 0; i < kNumFields; ++i) {
    bufs[i].resize(4096);
    res_bind[i].buffer_type = MYSQL_TYPE_STRING;
    res_bind[i].buffer = &bufs[i][0];
    res_bind[i].buffer_length = 4096;
    res_bind[i].length = &lens[i];
    res_bind[i].is_null = &nulls[i];
  }
  mysql_stmt_bind_result(stmt, res_bind.data());

  bool found = false;
  if (mysql_stmt_fetch(stmt) == 0) {
    found = true;
    int i = 0;
    out.id               = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.user_id          = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0; i++;
    out.filename         = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.original_filename= lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.storage_path     = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.description      = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.tags             = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.status           = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.error_msg        = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    out.file_size        = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0; i++;
    out.created_at       = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0; i++;
    out.updated_at       = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0;
  }
  mysql_stmt_close(stmt);
  if (!found) error = "not found";
  return found;
}

std::vector<ImageMaterialService::ImageMaterial> ImageMaterialService::List(
    std::uint64_t user_id, std::string& error) const {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "DB connection unavailable"; return {}; }

  const std::string sql =
      "SELECT id, user_id, filename, original_filename, storage_path, "
      "description, tags, status, error_msg, file_size, created_at, updated_at "
      "FROM image_materials WHERE user_id=? ORDER BY created_at DESC LIMIT 200";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "stmt init failed"; return {}; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return {};
  }

  MYSQL_BIND param[1];
  memset(param, 0, sizeof(param));
  param[0].buffer_type = MYSQL_TYPE_LONGLONG;
  param[0].buffer = &user_id;
  param[0].is_unsigned = true;

  if (mysql_stmt_bind_param(stmt, param) != 0 ||
      mysql_stmt_execute(stmt) != 0 ||
      mysql_stmt_store_result(stmt) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return {};
  }

  const int kNumFields = 12;
  std::vector<std::string> bufs(kNumFields);
  std::vector<unsigned long> lens(kNumFields, 0);
  std::unique_ptr<bool[]> nulls(new bool[kNumFields]());
  std::vector<MYSQL_BIND> res_bind(kNumFields);
  memset(res_bind.data(), 0, sizeof(MYSQL_BIND) * kNumFields);
  for (int i = 0; i < kNumFields; ++i) {
    bufs[i].resize(4096);
    res_bind[i].buffer_type = MYSQL_TYPE_STRING;
    res_bind[i].buffer = &bufs[i][0];
    res_bind[i].buffer_length = 4096;
    res_bind[i].length = &lens[i];
    res_bind[i].is_null = &nulls[i];
  }
  mysql_stmt_bind_result(stmt, res_bind.data());

  std::vector<ImageMaterial> list;
  while (mysql_stmt_fetch(stmt) == 0) {
    ImageMaterial m;
    int i = 0;
    m.id               = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.user_id          = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0; i++;
    m.filename         = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.original_filename= lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.storage_path     = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.description      = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.tags             = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.status           = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.error_msg        = lens[i] ? std::string(&bufs[i][0], lens[i]) : ""; i++;
    m.file_size        = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0; i++;
    m.created_at       = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0; i++;
    m.updated_at       = lens[i] ? std::stoull(std::string(&bufs[i][0], lens[i])) : 0;
    list.push_back(std::move(m));
  }
  mysql_stmt_close(stmt);
  return list;
}

bool ImageMaterialService::Delete(const std::string& image_id,
                                   std::uint64_t user_id,
                                   std::string& error) {
  ImageMaterial mat;
  std::string get_err;
  if (!Get(image_id, user_id, mat, get_err)) {
    error = get_err.empty() ? "image not found" : get_err;
    return false;
  }

  // Delete from Qdrant
  std::string q_err;
  RemoveFromQdrant(image_id, q_err);

  // Delete file
  if (!mat.storage_path.empty()) {
    std::error_code ec;
    std::filesystem::remove(mat.storage_path, ec);
  }

  // Delete from DB
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) { error = "DB connection unavailable"; return false; }

  const std::string sql = "DELETE FROM image_materials WHERE id=? AND user_id=?";
  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) { error = "stmt init failed"; return false; }
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }

  MYSQL_BIND params[2];
  memset(params, 0, sizeof(params));
  unsigned long id_len = image_id.size();
  params[0].buffer_type = MYSQL_TYPE_STRING;
  params[0].buffer = const_cast<char*>(image_id.data());
  params[0].buffer_length = image_id.size();
  params[0].length = &id_len;
  params[1].buffer_type = MYSQL_TYPE_LONGLONG;
  params[1].buffer = &user_id;
  params[1].is_unsigned = true;

  if (mysql_stmt_bind_param(stmt, params) != 0 ||
      mysql_stmt_execute(stmt) != 0) {
    error = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    return false;
  }
  mysql_stmt_close(stmt);
  return true;
}

bool ImageMaterialService::UpdateStatus(const std::string& image_id,
                                         const std::string& status,
                                         const std::string& description,
                                         const std::string& error_msg) {
  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) return false;

  const std::string sql =
      "UPDATE image_materials SET status=?, description=?, error_msg=?, updated_at=? "
      "WHERE id=?";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) return false;
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
    mysql_stmt_close(stmt);
    return false;
  }

  const auto now = NowSeconds();
  unsigned long s_len = status.size();
  unsigned long d_len = description.size();
  unsigned long e_len = error_msg.size();
  unsigned long id_len = image_id.size();
  uint64_t now_copy = now;

  MYSQL_BIND bind[5];
  memset(bind, 0, sizeof(bind));
  bind[0].buffer_type = MYSQL_TYPE_STRING;
  bind[0].buffer = const_cast<char*>(status.data());
  bind[0].buffer_length = status.size();
  bind[0].length = &s_len;

  bind[1].buffer_type = MYSQL_TYPE_STRING;
  bind[1].buffer = const_cast<char*>(description.data());
  bind[1].buffer_length = description.size();
  bind[1].length = &d_len;

  bind[2].buffer_type = MYSQL_TYPE_STRING;
  bind[2].buffer = const_cast<char*>(error_msg.data());
  bind[2].buffer_length = error_msg.size();
  bind[2].length = &e_len;

  bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
  bind[3].buffer = &now_copy;
  bind[3].is_unsigned = true;

  bind[4].buffer_type = MYSQL_TYPE_STRING;
  bind[4].buffer = const_cast<char*>(image_id.data());
  bind[4].buffer_length = image_id.size();
  bind[4].length = &id_len;

  const bool ok = (mysql_stmt_bind_param(stmt, bind) == 0 &&
                   mysql_stmt_execute(stmt) == 0);
  mysql_stmt_close(stmt);
  return ok;
}

bool ImageMaterialService::IndexToQdrant(const std::string& image_id,
                                          std::uint64_t user_id,
                                          const std::string& description,
                                          const std::string& filename,
                                          const std::string& storage_path,
                                          std::string& error) {
  const auto embedding = qwen_client_->GetEmbedding(description);
  if (embedding.empty()) {
    error = "embedding failed for description";
    return false;
  }

  nlohmann::json vec_arr = nlohmann::json::array();
  for (float v : embedding) vec_arr.push_back(v);

  const std::uint64_t point_id = StrToUint64Hash(image_id);
  nlohmann::json point = {
    {"id", point_id},
    {"vector", vec_arr},
    {"payload", {
      {"image_id",     image_id},
      {"user_id",      user_id},
      {"filename",     filename},
      {"description",  Utf8SafeTruncate(description, 1000)},
      {"storage_path", storage_path},
      {"status",       "ready"}
    }}
  };

  nlohmann::json body = {{"points", nlohmann::json::array({point})}};
  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points";
  const std::string resp = DoQdrantRequest("PUT", path, body.dump(), code);
  if (code == 200 || code == 201) return true;
  error = "Qdrant upsert failed, HTTP " + std::to_string(code) + ": " + resp.substr(0, 200);
  return false;
}

bool ImageMaterialService::RemoveFromQdrant(const std::string& image_id, std::string& error) {
  nlohmann::json body = {
    {"filter", {
      {"must", nlohmann::json::array({
        {{"key", "image_id"}, {"match", {{"value", image_id}}}}
      })}
    }}
  };
  int code = 0;
  const std::string path = "/collections/" + std::string(kCollectionName) + "/points/delete";
  const std::string resp = DoQdrantRequest("POST", path, body.dump(), code);
  if (code == 200) return true;
  error = "RemoveFromQdrant failed, HTTP " + std::to_string(code);
  return false;
}

std::string ImageMaterialService::DoQdrantRequest(const std::string& method,
                                                   const std::string& path,
                                                   const std::string& body,
                                                   int& http_code) const {
  CURL* curl = curl_easy_init();
  if (!curl) { http_code = -1; return {}; }

  std::string response_body;
  const std::string url = qdrant_base_url_ + path;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  if (method == "GET") {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  } else if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  }

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_perform(curl);

  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  http_code = static_cast<int>(code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return response_body;
}
