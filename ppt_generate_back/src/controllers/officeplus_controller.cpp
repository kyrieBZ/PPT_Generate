#include "controllers/officeplus_controller.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"
#include "services/template_fastdfs_service.h"

namespace {

std::string ExtractToken(const HttpRequest& request) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  if (!header.empty()) return header;
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    return it->second;
  }
  return {};
}

/** 将参数列表中所有单引号转义，防止 shell 注入（用于 popen 命令拼接）。 */
std::string ShellEscapeSingleQuote(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 10);
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else           out += c;
  }
  return out;
}

}  // namespace

OfficePlusController::OfficePlusController(
    std::shared_ptr<AuthService>             auth_service,
    std::shared_ptr<TemplateService>         template_service,
    const std::string&                       python_binary,
    const std::string&                       catalog_path,
    const std::string&                       templates_dir,
    const std::string&                       thumbnails_dir,
    const std::string&                       fetcher_script,
    std::shared_ptr<TemplateFastDfsService>  tmpl_fastdfs_service)
    : auth_service_(std::move(auth_service)),
      template_service_(std::move(template_service)),
      tmpl_fastdfs_service_(std::move(tmpl_fastdfs_service)),
      python_binary_(python_binary),
      catalog_path_(catalog_path),
      templates_dir_(templates_dir),
      thumbnails_dir_(thumbnails_dir),
      fetcher_script_(fetcher_script) {}

bool OfficePlusController::ExtractThumbnail(const std::string& pptx_path,
                                             const std::string& out_dir,
                                             std::string&       out_png_path,
                                             std::string&       error) const {
  // 用 LibreOffice 将 pptx 转为图片（只取第一张）
  // 命令：libreoffice --headless --convert-to png --outdir <out_dir> <pptx>
  // LibreOffice 输出文件名规则：<stem>1.png（多页则有1,2,3...，只需第一张）
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);

  std::ostringstream cmd;
  cmd << "libreoffice --headless --convert-to png"
      << " --outdir '" << ShellEscapeSingleQuote(out_dir) << "'"
      << " '" << ShellEscapeSingleQuote(pptx_path) << "'"
      << " 2>/dev/null";

  int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    error = "LibreOffice 转换失败（exit=" + std::to_string(ret) + "）";
    return false;
  }

  // 查找输出文件：收集 out_dir 下所有 .png 文件，按文件名排序取第一个
  // LibreOffice 命名规则：单页输出 <stem>.png；多页输出 <stem>1.png, <stem>2.png ...
  std::vector<std::string> png_files;
  for (auto& entry : std::filesystem::directory_iterator(out_dir, ec)) {
    const auto& p = entry.path();
    const auto ext = p.extension().string();
    if (ext == ".png" || ext == ".PNG") {
      png_files.push_back(p.string());
    }
  }
  if (png_files.empty()) {
    error = "LibreOffice 转换后未找到 PNG 输出文件（outdir=" + out_dir + "）";
    return false;
  }
  // 按文件名字母序排序，取最小的（即第一张幻灯片）
  std::sort(png_files.begin(), png_files.end());
  out_png_path = png_files.front();
  return true;
}

std::shared_ptr<User> OfficePlusController::AuthenticateAdmin(
    const HttpRequest& request, std::string& error) const {
  const auto token = ExtractToken(request);
  if (token.empty()) { error = "Token not provided"; return nullptr; }
  auto user = auth_service_->GetUserFromToken(token, error);
  if (!user) { error = error.empty() ? "Invalid token" : error; return nullptr; }
  if (!user->is_admin) { error = "Forbidden"; return nullptr; }
  return std::make_shared<User>(*user);
}

std::string OfficePlusController::RunFetcher(
    const std::vector<std::string>& args, std::string& error) const {
  std::ostringstream cmd;
  cmd << "'" << ShellEscapeSingleQuote(python_binary_) << "' "
      << "'" << ShellEscapeSingleQuote(fetcher_script_) << "'";
  for (const auto& a : args) {
    cmd << " '" << ShellEscapeSingleQuote(a) << "'";
  }
  cmd << " 2>/dev/null";

  const std::string cmd_str = cmd.str();
  Logger::Info("RunFetcher: " + cmd_str.substr(0, 400));

  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd_str.c_str(), "r"), pclose);
  if (!pipe) {
    error = "popen failed";
    return {};
  }

  std::string output;
  std::array<char, 4096> buf{};
  while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) {
    output += buf.data();
  }
  return output;
}

HttpResponse OfficePlusController::Search(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string keyword, tag, cookie;
  int page = 1, page_size = 20;

  if (auto it = request.query_params.find("keyword"); it != request.query_params.end())
    keyword = it->second;
  if (auto it = request.query_params.find("tag"); it != request.query_params.end())
    tag = it->second;
  if (auto it = request.query_params.find("page"); it != request.query_params.end()) {
    try { page = std::stoi(it->second); } catch (...) {}
  }
  if (auto it = request.query_params.find("page_size"); it != request.query_params.end()) {
    try { page_size = std::stoi(it->second); } catch (...) {}
  }
  if (auto it = request.query_params.find("cookie"); it != request.query_params.end())
    cookie = it->second;

  std::vector<std::string> args = {
    "search",
    "--keyword", keyword,
    "--page", std::to_string(page),
    "--page_size", std::to_string(page_size),
  };
  if (!tag.empty()) { args.push_back("--tag"); args.push_back(tag); }
  if (!cookie.empty()) { args.push_back("--cookie"); args.push_back(cookie); }

  std::string out = RunFetcher(args, error);
  if (!error.empty()) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "脚本执行失败：" + error));
  }

  try {
    auto j = nlohmann::json::parse(out);
    return HttpResponse::Json(200, j);
  } catch (...) {
    Logger::Error("OfficePlus Search: invalid JSON output: " + out.substr(0, 200));
    return HttpResponse::Json(502, ErrorJson("ERR_PARSE", "返回数据解析失败"));
  }
}

HttpResponse OfficePlusController::Info(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string url, tmpl_id, cookie;
  if (auto it = request.query_params.find("url"); it != request.query_params.end())
    url = it->second;
  if (auto it = request.query_params.find("id"); it != request.query_params.end())
    tmpl_id = it->second;
  if (auto it = request.query_params.find("cookie"); it != request.query_params.end())
    cookie = it->second;

  if (url.empty() && tmpl_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "请提供 url 或 id 参数"));
  }

  std::vector<std::string> args = {"info"};
  if (!url.empty())     { args.push_back("--url"); args.push_back(url); }
  if (!tmpl_id.empty()) { args.push_back("--id");  args.push_back(tmpl_id); }
  if (!cookie.empty())  { args.push_back("--cookie"); args.push_back(cookie); }

  std::string out = RunFetcher(args, error);
  if (!error.empty()) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "脚本执行失败：" + error));
  }

  try {
    auto j = nlohmann::json::parse(out);
    if (j.contains("error")) {
      return HttpResponse::Json(422, ErrorJson("ERR_FETCH", j["error"].get<std::string>()));
    }
    return HttpResponse::Json(200, j);
  } catch (...) {
    return HttpResponse::Json(502, ErrorJson("ERR_PARSE", "返回数据解析失败"));
  }
}

HttpResponse OfficePlusController::Import(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  try {
    auto body = nlohmann::json::parse(request.body);

    std::string url      = body.value("url", "");
    std::string tmpl_id  = body.value("id", "");
    std::string custom_id = body.value("customId", "");
    std::string cookie   = body.value("cookie", "");

    if (url.empty() && tmpl_id.empty()) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "请提供 url 或 id"));
    }

    std::vector<std::string> args = {
      "download",
      "--catalog",       catalog_path_,
      "--templates_dir", templates_dir_,
      "--thumbnails_dir", thumbnails_dir_,
    };
    if (!url.empty())       { args.push_back("--url");       args.push_back(url); }
    if (!tmpl_id.empty())   { args.push_back("--id");        args.push_back(tmpl_id); }
    if (!custom_id.empty()) { args.push_back("--custom_id"); args.push_back(custom_id); }
    if (!cookie.empty())    { args.push_back("--cookie");    args.push_back(cookie); }

    std::string out = RunFetcher(args, error);
    if (!error.empty()) {
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "脚本执行失败：" + error));
    }

    try {
      auto j = nlohmann::json::parse(out);
      if (j.contains("error") && !j.value("success", false)) {
        // 区分"下载失败但已写 catalog"和"完全失败"
        int status = j.value("success", false) ? 200 : 422;
        return HttpResponse::Json(status, j);
      }

      // catalog 已更新，热重载 TemplateService（无需重启）
      if (template_service_) {
        std::string reload_error;
        int cnt = template_service_->Reload(reload_error);
        if (cnt >= 0) {
          Logger::Info("OfficePLUS import: catalog reloaded, " + std::to_string(cnt) + " templates.");
          j["catalogReloaded"] = true;
          j["catalogSize"] = cnt;
        } else {
          Logger::Warn("OfficePLUS import: catalog reload failed: " + reload_error);
          j["catalogReloaded"] = false;
        }
      }

      return HttpResponse::Json(200, j);
    } catch (...) {
      Logger::Error("OfficePlus Import: invalid JSON: " + out.substr(0, 300));
      return HttpResponse::Json(502, ErrorJson("ERR_PARSE", "脚本返回数据解析失败"));
    }
  } catch (const std::exception& ex) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", std::string("Invalid JSON: ") + ex.what()));
  }
}

// Base64 decode helper (RFC 4648, no padding variation)
static std::string Base64Decode(const std::string& b64) {
  static const std::string kChars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(b64.size() * 3 / 4);
  int val = 0, bits = -8;
  for (unsigned char c : b64) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    auto pos = kChars.find(c);
    if (pos == std::string::npos) continue;
    val = (val << 6) + static_cast<int>(pos);
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

HttpResponse OfficePlusController::UploadFile(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  // 期望 JSON body：{ "template_id": "op-552", "file_base64": "<base64编码的pptx>" }
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "请求体必须是 JSON"));
  }

  std::string template_id = body.value("template_id", "");
  std::string file_base64 = body.value("file_base64", "");

  if (template_id.empty())
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 template_id 字段"));
  if (file_base64.empty())
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 file_base64 字段"));

  std::string file_data = Base64Decode(file_base64);

  if (file_data.size() < 4 ||
      (unsigned char)file_data[0] != 'P' || (unsigned char)file_data[1] != 'K') {
    return HttpResponse::Json(400, ErrorJson("ERR_INVALID_FILE", "文件不是有效的 pptx（ZIP）格式"));
  }

  // 保存到 templates_dir/template_id.pptx
  const std::filesystem::path dest_path =
      std::filesystem::path(templates_dir_) / (template_id + ".pptx");
  try {
    std::filesystem::create_directories(dest_path.parent_path());
    std::ofstream ofs(dest_path, std::ios::binary);
    if (!ofs) throw std::runtime_error("无法创建文件: " + dest_path.string());
    ofs.write(file_data.data(), static_cast<std::streamsize>(file_data.size()));
    ofs.close();
  } catch (const std::exception& ex) {
    return HttpResponse::Json(500, ErrorJson("ERR_WRITE", std::string("文件写入失败: ") + ex.what()));
  }

  // 更新 catalog：将 local_file 字段填入相对路径
  try {
    nlohmann::json catalog;
    {
      std::ifstream ifs(catalog_path_);
      if (!ifs) throw std::runtime_error("无法读取 catalog: " + catalog_path_);
      ifs >> catalog;
    }

    bool found = false;
    std::filesystem::path cat_dir = std::filesystem::path(catalog_path_).parent_path();
    std::string rel_local;
    try {
      rel_local = std::filesystem::relative(dest_path, cat_dir).string();
    } catch (...) {
      rel_local = dest_path.string();
    }

    for (auto& entry : catalog) {
      if (entry.value("id", "") == template_id) {
        entry["local_file"] = rel_local;
        found = true;
        break;
      }
    }

    if (!found) {
      return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND",
          "catalog 中找不到 id=" + template_id + "，请先通过「预览并导入」创建记录"));
    }

    std::ofstream ofs(catalog_path_);
    if (!ofs) throw std::runtime_error("无法写入 catalog");
    ofs << catalog.dump(2);
    ofs.close();
  } catch (const std::exception& ex) {
    return HttpResponse::Json(500, ErrorJson("ERR_CATALOG", std::string("catalog 更新失败: ") + ex.what()));
  }

  std::string reload_error;
  int cnt = template_service_->Reload(reload_error);
  Logger::Info("OfficePLUS upload: file saved and catalog reloaded, " +
               std::to_string(cnt) + " templates. id=" + template_id);

  return HttpResponse::Json(200, {
    {"success", true},
    {"templateId", template_id},
    {"savedPath", dest_path.string()},
    {"fileSize", static_cast<int>(file_data.size())},
    {"catalogReloaded", cnt >= 0},
    {"catalogSize", cnt},
    {"message", "pptx 上传成功，catalog 已更新"}
  });
}

HttpResponse OfficePlusController::BatchUpload(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  // Body: { "files": [ { "filename": "商务年终.pptx", "file_base64": "..." }, ... ] }
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "请求体必须是 JSON"));
  }

  if (!body.contains("files") || !body["files"].is_array() || body["files"].empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 files 数组"));
  }

  // 读取现有 catalog
  nlohmann::json catalog = nlohmann::json::array();
  if (std::filesystem::exists(catalog_path_)) {
    try {
      std::ifstream ifs(catalog_path_);
      if (ifs) ifs >> catalog;
    } catch (...) {}
  }

  std::filesystem::path cat_dir = std::filesystem::path(catalog_path_).parent_path();
  std::filesystem::create_directories(std::filesystem::path(templates_dir_));

  nlohmann::json results = nlohmann::json::array();
  int success_count = 0;

  for (const auto& item : body["files"]) {
    std::string orig_filename = item.value("filename", "");
    std::string file_base64  = item.value("file_base64", "");

    if (orig_filename.empty() || file_base64.empty()) {
      results.push_back({{"filename", orig_filename}, {"ok", false}, {"error", "缺少 filename 或 file_base64"}});
      continue;
    }

    // 解码
    std::string file_data = Base64Decode(file_base64);
    if (file_data.size() < 4 ||
        (unsigned char)file_data[0] != 'P' || (unsigned char)file_data[1] != 'K') {
      results.push_back({{"filename", orig_filename}, {"ok", false}, {"error", "不是有效的 pptx（ZIP）格式"}});
      continue;
    }

    // 从文件名生成 template_id：去掉扩展名，加 up- 前缀，替换特殊字符
    std::string stem = orig_filename;
    auto dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    // 简单清洗：只保留字母数字汉字连字符
    std::string clean_id;
    for (unsigned char c : stem) {
      if (std::isalnum(c) || c == '-' || c == '_' || c >= 0x80) {
        clean_id += c;
      } else {
        clean_id += '-';
      }
    }
    // 截断过长
    if (clean_id.size() > 48) clean_id = clean_id.substr(0, 48);
    const std::string template_id = "up-" + clean_id;

    // 保存文件
    const std::filesystem::path dest_path =
        std::filesystem::path(templates_dir_) / (template_id + ".pptx");
    try {
      std::ofstream ofs(dest_path, std::ios::binary);
      if (!ofs) throw std::runtime_error("无法创建文件");
      ofs.write(file_data.data(), static_cast<std::streamsize>(file_data.size()));
      ofs.close();
    } catch (const std::exception& ex) {
      results.push_back({{"filename", orig_filename}, {"ok", false},
                         {"templateId", template_id}, {"error", ex.what()}});
      continue;
    }

    // 计算 catalog 相对路径
    std::string rel_local;
    try {
      rel_local = std::filesystem::relative(dest_path, cat_dir).string();
    } catch (...) {
      rel_local = dest_path.string();
    }

    // 更新或新增 catalog 条目
    // 优先使用前端传入的展示名；若为空则回退到文件名 stem
    std::string display_name = item.value("display_name", "");
    if (display_name.empty()) display_name = stem;
    const std::string tmpl_name = display_name.empty() ? template_id : display_name;
    nlohmann::json new_entry = {
      {"id",           template_id},
      {"name",         tmpl_name},
      {"provider",     "upload"},
      {"provider_url", ""},
      {"description",  ""},
      {"preview_image", ""},
      {"download_url", ""},
      {"license",      ""},
      {"tags",         nlohmann::json::array()},
      {"theme",        {{"primary_color","#1e293b"},{"secondary_color","#334155"},
                        {"accent_color","#6366f1"},{"background_image",""}}},
      {"local_file",   rel_local},
    };

    bool found = false;
    for (auto& entry : catalog) {
      if (entry.value("id", "") == template_id) {
        entry["local_file"] = rel_local;
        entry["name"]       = tmpl_name;
        found = true;
        break;
      }
    }
    if (!found) catalog.push_back(new_entry);

    // ── FastDFS 异步：后台线程完成缩略图提取 + FastDFS 上传，不阻塞 HTTP 响应 ──
    if (tmpl_fastdfs_service_) {
      // 捕获值拷贝，避免悬挂引用
      const std::string  async_tmpl_id    = template_id;
      const std::string  async_pptx_path  = dest_path.string();
      const std::string  async_thumb_dir  = std::filesystem::path(thumbnails_dir_).string()
                                            + "/tmp_" + template_id;
      const std::string  async_final_dir  = thumbnails_dir_;
      auto               async_fdfs_svc   = tmpl_fastdfs_service_;

      // ExtractThumbnail 是成员函数，需用 shared_ptr 延长 controller 生命周期
      // 这里直接在 lambda 中复制所需数据，用 [=] 捕获
      std::thread([=]() {
        // 1. 提取缩略图
        std::string thumb_path;
        std::string thumb_error;
        // 重新构造临时逻辑（无法调用成员函数，需复制其逻辑）
        std::filesystem::create_directories(async_thumb_dir);
        const std::string cmd = "libreoffice --headless --convert-to png"
                                " --outdir " + async_thumb_dir
                                + " \"" + async_pptx_path + "\""
                                + " > /dev/null 2>&1";
        int ret = std::system(cmd.c_str());
        bool thumb_ok = false;
        if (ret == 0) {
          std::error_code ec_dir;
          std::vector<std::filesystem::path> pngs;
          for (auto& e : std::filesystem::directory_iterator(async_thumb_dir, ec_dir)) {
            if (e.path().extension() == ".png") pngs.push_back(e.path());
          }
          std::sort(pngs.begin(), pngs.end());
          if (!pngs.empty()) {
            const std::filesystem::path final_thumb =
                std::filesystem::path(async_final_dir) / (async_tmpl_id + ".png");
            std::error_code ec2;
            std::filesystem::create_directories(async_final_dir, ec2);
            std::filesystem::copy_file(pngs[0], final_thumb,
                                       std::filesystem::copy_options::overwrite_existing, ec2);
            std::filesystem::remove_all(async_thumb_dir, ec2);
            if (!ec2) {
              thumb_path = final_thumb.string();
              thumb_ok   = true;
            }
          }
        }
        if (!thumb_ok) {
          Logger::Warn("BatchUpload[async]: thumbnail extraction failed for " + async_tmpl_id);
          std::error_code ec2;
          std::filesystem::remove_all(async_thumb_dir, ec2);
        }

        // 2. 上传 pptx + 缩略图到 FastDFS
        std::string fdfs_error;
        if (async_fdfs_svc->UploadTemplate(
                async_tmpl_id,
                async_pptx_path,
                thumb_ok ? thumb_path : "",
                fdfs_error)) {
          auto entry = async_fdfs_svc->GetEntry(async_tmpl_id);
          Logger::Info("BatchUpload[async]: FastDFS upload ok for " + async_tmpl_id
                       + " pptx=" + (entry ? entry->pptx_url : "?")
                       + " thumb=" + (entry ? entry->thumbnail_url : "?"));
        } else {
          Logger::Warn("BatchUpload[async]: FastDFS upload failed for " + async_tmpl_id + ": " + fdfs_error);
        }
      }).detach();
    }

    results.push_back({
        {"filename",   orig_filename},
        {"ok",         true},
        {"templateId", template_id},
        {"fileSize",   static_cast<int>(file_data.size())},
        {"asyncFastDfs", tmpl_fastdfs_service_ != nullptr},
    });
    success_count++;
  }

  // 写回 catalog
  try {
    std::ofstream ofs(catalog_path_);
    if (!ofs) throw std::runtime_error("无法写入 catalog");
    ofs << catalog.dump(2);
    ofs.close();
  } catch (const std::exception& ex) {
    return HttpResponse::Json(500, ErrorJson("ERR_CATALOG", std::string("catalog 写入失败: ") + ex.what()));
  }

  // 热重载
  std::string reload_error;
  int cnt = template_service_->Reload(reload_error);
  Logger::Info("OfficePLUS batch upload: " + std::to_string(success_count) +
               "/" + std::to_string(body["files"].size()) + " files OK, catalog=" + std::to_string(cnt));

  return HttpResponse::Json(200, {
    {"success", true},
    {"total",   static_cast<int>(body["files"].size())},
    {"ok",      success_count},
    {"failed",  static_cast<int>(body["files"].size()) - success_count},
    {"results", results},
    {"catalogSize", cnt},
    {"message", std::to_string(success_count) + " 个模板上传成功"}
  });
}

// ── multipart/form-data 辅助（与 material_controller 相同逻辑，局部复制避免跨文件依赖） ──

namespace {

std::string MpExtractBoundary(const std::string& content_type) {
  const std::string key = "boundary=";
  auto pos = content_type.find(key);
  if (pos == std::string::npos) return {};
  std::string b = content_type.substr(pos + key.size());
  if (!b.empty() && b.front() == '"') {
    b = b.substr(1);
    auto q = b.find('"');
    if (q != std::string::npos) b.resize(q);
  }
  auto semi = b.find(';');
  if (semi != std::string::npos) b.resize(semi);
  // trim
  while (!b.empty() && (b.front() == ' ' || b.front() == '\t')) b.erase(b.begin());
  while (!b.empty() && (b.back()  == ' ' || b.back()  == '\t')) b.pop_back();
  return b;
}

struct MpPart {
  std::string name;
  std::string filename;
  std::string data;
};

std::vector<MpPart> MpParse(const std::string& body, const std::string& boundary) {
  std::vector<MpPart> parts;
  if (boundary.empty()) return parts;
  const std::string delim = "--" + boundary;
  std::size_t pos = 0;
  while (pos < body.size()) {
    auto dp = body.find(delim, pos);
    if (dp == std::string::npos) break;
    std::size_t after = dp + delim.size();
    if (after + 1 < body.size() && body[after] == '-' && body[after + 1] == '-') break;
    if (after < body.size() && body[after] == '\r') ++after;
    if (after < body.size() && body[after] == '\n') ++after;
    auto hend = body.find("\r\n\r\n", after);
    if (hend == std::string::npos) break;
    const std::string hdrs = body.substr(after, hend - after);
    std::size_t dstart = hend + 4;
    auto ndp = body.find("\r\n" + delim, dstart);
    std::size_t dend = (ndp == std::string::npos) ? body.size() : ndp;
    MpPart part;
    part.data = body.substr(dstart, dend - dstart);
    std::istringstream hs(hdrs);
    std::string hl;
    while (std::getline(hs, hl)) {
      if (!hl.empty() && hl.back() == '\r') hl.pop_back();
      auto colon = hl.find(':');
      if (colon == std::string::npos) continue;
      std::string hk = hl.substr(0, colon);
      std::transform(hk.begin(), hk.end(), hk.begin(), [](unsigned char c){ return std::tolower(c); });
      std::string hv = hl.substr(colon + 1);
      while (!hv.empty() && (hv.front() == ' ' || hv.front() == '\t')) hv.erase(hv.begin());
      if (hk == "content-disposition") {
        std::istringstream ds(hv);
        std::string tok;
        while (std::getline(ds, tok, ';')) {
          while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.erase(tok.begin());
          while (!tok.empty() && (tok.back()  == ' ' || tok.back()  == '\t')) tok.pop_back();
          if (tok.rfind("name=", 0) == 0) {
            part.name = tok.substr(5);
            if (!part.name.empty() && part.name.front() == '"') {
              part.name = part.name.substr(1);
              auto q = part.name.find('"');
              if (q != std::string::npos) part.name.resize(q);
            }
          } else if (tok.rfind("filename=", 0) == 0) {
            part.filename = tok.substr(9);
            if (!part.filename.empty() && part.filename.front() == '"') {
              part.filename = part.filename.substr(1);
              auto q = part.filename.find('"');
              if (q != std::string::npos) part.filename.resize(q);
            }
          }
        }
      }
    }
    if (!part.name.empty()) parts.push_back(std::move(part));
    pos = dend;
  }
  return parts;
}

}  // namespace

// ── BatchUploadForm（multipart/form-data 版）────────────────────────────────
// 请求格式（每次一个文件，前端并发多请求）：
//   Content-Type: multipart/form-data; boundary=...
//   字段 "file"         : pptx 二进制数据（filename 即原始文件名）
//   字段 "display_name" : 可选，前端中文展示名
HttpResponse OfficePlusController::BatchUploadForm(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  const std::string content_type = request.Header("content-type");
  const std::string boundary = MpExtractBoundary(content_type);
  if (boundary.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Content-Type 必须为 multipart/form-data"));
  }

  const auto parts = MpParse(request.body, boundary);
  if (parts.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "请求中没有有效的 multipart 字段"));
  }

  // 取 file 字段和 display_name 字段
  std::string orig_filename, file_data, display_name;
  for (const auto& p : parts) {
    if (p.name == "file" && !p.filename.empty()) {
      orig_filename = p.filename;
      file_data     = p.data;
    } else if (p.name == "display_name") {
      display_name = p.data;
      // trim
      while (!display_name.empty() && (display_name.back() == '\r' || display_name.back() == '\n' || display_name.back() == ' '))
        display_name.pop_back();
    }
  }

  if (orig_filename.empty() || file_data.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 file 字段或文件为空"));
  }

  // 验证 PPTX 格式（ZIP magic bytes）
  if (file_data.size() < 4 ||
      (unsigned char)file_data[0] != 'P' || (unsigned char)file_data[1] != 'K') {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "不是有效的 pptx（ZIP）格式"));
  }

  // 读取现有 catalog
  nlohmann::json catalog = nlohmann::json::array();
  if (std::filesystem::exists(catalog_path_)) {
    try {
      std::ifstream ifs(catalog_path_);
      if (ifs) ifs >> catalog;
    } catch (...) {}
  }

  std::filesystem::path cat_dir = std::filesystem::path(catalog_path_).parent_path();
  std::filesystem::create_directories(std::filesystem::path(templates_dir_));

  // 生成 template_id
  std::string stem = orig_filename;
  auto dot = stem.rfind('.');
  if (dot != std::string::npos) stem = stem.substr(0, dot);
  std::string clean_id;
  for (unsigned char c : stem) {
    if (std::isalnum(c) || c == '-' || c == '_' || c >= 0x80) clean_id += c;
    else clean_id += '-';
  }
  if (clean_id.size() > 48) clean_id = clean_id.substr(0, 48);
  const std::string template_id = "up-" + clean_id;

  // 保存文件
  const std::filesystem::path dest_path =
      std::filesystem::path(templates_dir_) / (template_id + ".pptx");
  try {
    std::ofstream ofs(dest_path, std::ios::binary);
    if (!ofs) throw std::runtime_error("无法创建文件");
    ofs.write(file_data.data(), static_cast<std::streamsize>(file_data.size()));
  } catch (const std::exception& ex) {
    return HttpResponse::Json(500, ErrorJson("ERR_SAVE", std::string("文件保存失败: ") + ex.what()));
  }

  // catalog 相对路径
  std::string rel_local;
  try {
    rel_local = std::filesystem::relative(dest_path, cat_dir).string();
  } catch (...) {
    rel_local = dest_path.string();
  }

  // 确定展示名
  if (display_name.empty()) display_name = stem;
  const std::string tmpl_name = display_name.empty() ? template_id : display_name;

  nlohmann::json new_entry = {
    {"id",           template_id},
    {"name",         tmpl_name},
    {"provider",     "upload"},
    {"provider_url", ""},
    {"description",  ""},
    {"preview_image", ""},
    {"download_url", ""},
    {"license",      ""},
    {"tags",         nlohmann::json::array()},
    {"theme",        {{"primary_color","#1e293b"},{"secondary_color","#334155"},
                      {"accent_color","#6366f1"},{"background_image",""}}},
    {"local_file",   rel_local},
  };

  bool found = false;
  for (auto& entry : catalog) {
    if (entry.value("id", "") == template_id) {
      entry["local_file"] = rel_local;
      entry["name"]       = tmpl_name;
      found = true;
      break;
    }
  }
  if (!found) catalog.push_back(new_entry);

  // 写回 catalog
  try {
    std::ofstream ofs(catalog_path_);
    if (!ofs) throw std::runtime_error("无法写入 catalog");
    ofs << catalog.dump(2);
  } catch (const std::exception& ex) {
    return HttpResponse::Json(500, ErrorJson("ERR_CATALOG", std::string("catalog 写入失败: ") + ex.what()));
  }

  // 热重载
  std::string reload_error;
  int cnt = template_service_->Reload(reload_error);
  Logger::Info("BatchUploadForm: saved " + template_id + " (" + tmpl_name + "), catalog=" + std::to_string(cnt));

  // 异步 FastDFS 上传（缩略图提取 + 上传）
  if (tmpl_fastdfs_service_) {
    const std::string  async_tmpl_id   = template_id;
    const std::string  async_pptx_path = dest_path.string();
    const std::string  async_thumb_dir = std::filesystem::path(thumbnails_dir_).string() + "/tmp_" + template_id;
    const std::string  async_final_dir = thumbnails_dir_;
    auto               async_fdfs_svc  = tmpl_fastdfs_service_;

    std::thread([=]() {
      std::filesystem::create_directories(async_thumb_dir);
      const std::string cmd = "libreoffice --headless --convert-to png"
                              " --outdir " + async_thumb_dir
                              + " \"" + async_pptx_path + "\""
                              + " > /dev/null 2>&1";
      int ret = std::system(cmd.c_str());
      bool thumb_ok = false;
      std::string thumb_path;
      if (ret == 0) {
        std::error_code ec;
        std::vector<std::filesystem::path> pngs;
        for (auto& e : std::filesystem::directory_iterator(async_thumb_dir, ec))
          if (e.path().extension() == ".png") pngs.push_back(e.path());
        std::sort(pngs.begin(), pngs.end());
        if (!pngs.empty()) {
          const std::filesystem::path ft =
              std::filesystem::path(async_final_dir) / (async_tmpl_id + ".png");
          std::error_code ec2;
          std::filesystem::create_directories(async_final_dir, ec2);
          std::filesystem::copy_file(pngs[0], ft,
                                     std::filesystem::copy_options::overwrite_existing, ec2);
          std::filesystem::remove_all(async_thumb_dir, ec2);
          if (!ec2) { thumb_path = ft.string(); thumb_ok = true; }
        }
      }
      if (!thumb_ok) {
        std::error_code ec2;
        std::filesystem::remove_all(async_thumb_dir, ec2);
        Logger::Warn("BatchUploadForm[async]: thumbnail failed for " + async_tmpl_id);
      }
      std::string fdfs_error;
      if (async_fdfs_svc->UploadTemplate(async_tmpl_id, async_pptx_path,
                                          thumb_ok ? thumb_path : "", fdfs_error)) {
        auto entry = async_fdfs_svc->GetEntry(async_tmpl_id);
        Logger::Info("BatchUploadForm[async]: FastDFS ok for " + async_tmpl_id
                     + " pptx=" + (entry ? entry->pptx_url : "?")
                     + " thumb=" + (entry ? entry->thumbnail_url : "?"));
      } else {
        Logger::Warn("BatchUploadForm[async]: FastDFS failed for " + async_tmpl_id + ": " + fdfs_error);
      }
    }).detach();
  }

  return HttpResponse::Json(200, {
    {"success",      true},
    {"templateId",   template_id},
    {"name",         tmpl_name},
    {"filename",     orig_filename},
    {"fileSize",     static_cast<int>(file_data.size())},
    {"asyncFastDfs", tmpl_fastdfs_service_ != nullptr},
    {"catalogSize",  cnt},
  });
}

HttpResponse OfficePlusController::Reload(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string reload_error;
  int cnt = template_service_->Reload(reload_error);
  if (cnt < 0) {
    Logger::Error("Catalog reload failed: " + reload_error);
    return HttpResponse::Json(500, ErrorJson("ERR_RELOAD", "Catalog 热重载失败：" + reload_error));
  }
  Logger::Info("Admin triggered catalog reload: " + std::to_string(cnt) + " templates.");
  return HttpResponse::Json(200, {
    {"success", true},
    {"catalogSize", cnt},
    {"message", "Catalog 已热重载，" + std::to_string(cnt) + " 个模板立即生效"}
  });
}
