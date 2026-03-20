#include "controllers/settings_controller.h"

#include <sstream>
#include <string>
#include <vector>

#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"

namespace {

// ── 可配置项元数据定义 ──────────────────────────────────────────────────────────
struct SettingMeta {
  const char* key;
  const char* default_value;
  const char* type;   // "bool" | "int" | "string"
  const char* label;
  const char* description;
  const char* group;  // 前端分组
};

// 所有可热更新的配置项（与 system_settings 表对应）
static const SettingMeta kDefaultSettings[] = {
  // ── 基本配置 ─────────────────────────────────────────────────────────────
  {
    "registration_enabled",
    "true",
    "bool",
    "允许新用户注册",
    "关闭后新访客无法注册账户，已注册用户不受影响",
    "basic"
  },
  {
    "site_name",
    "PPT智能生成系统",
    "string",
    "站点名称",
    "显示在页面标题和邮件中的系统名称",
    "basic"
  },
  {
    "maintenance_mode",
    "false",
    "bool",
    "维护模式",
    "开启后所有用户（除管理员外）将看到维护提示，无法使用功能",
    "basic"
  },

  // ── 生成限制 ─────────────────────────────────────────────────────────────
  {
    "daily_generation_limit",
    "0",
    "int",
    "单用户每日生成上限",
    "每个用户每天最多可生成 PPT 的次数，0 表示不限制",
    "limits"
  },
  {
    "max_pages_per_request",
    "30",
    "int",
    "单次最大页数",
    "每次生成请求允许的最大幻灯片页数（建议不超过 50）",
    "limits"
  },
  {
    "max_concurrent_jobs",
    "4",
    "int",
    "最大并发生成任务数",
    "系统同时处理的 PPT 生成任务上限，超出后新请求进入队列",
    "limits"
  },
  {
    "generation_timeout_minutes",
    "10",
    "int",
    "生成超时时间（分钟）",
    "单次 PPT 生成任务超过此时长将被标记为失败",
    "limits"
  },

  // ── AI 模型配置 ───────────────────────────────────────────────────────────
  {
    "default_model_key",
    "qwen-plus",
    "string",
    "默认 AI 模型",
    "用户未手动选择模型时使用的默认模型键名",
    "ai"
  },
  {
    "enable_image_generation",
    "true",
    "bool",
    "允许配图生成",
    "是否允许用户在生成 PPT 时开启 AI 配图功能",
    "ai"
  },
  {
    "enable_speaker_notes",
    "true",
    "bool",
    "允许演讲备注生成",
    "是否允许用户在生成 PPT 时开启演讲者备注功能",
    "ai"
  },
};

static const std::size_t kSettingCount =
    sizeof(kDefaultSettings) / sizeof(kDefaultSettings[0]);

// ── 工具函数 ──────────────────────────────────────────────────────────────────

/** 对单引号做最小转义，防止 SQL 注入 */
std::string EscapeStr(MYSQL* conn, const std::string& s) {
  std::string out(s.size() * 2 + 1, '\0');
  unsigned long len = mysql_real_escape_string(
      conn, out.data(), s.c_str(), static_cast<unsigned long>(s.size()));
  out.resize(len);
  return out;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

SettingsController::SettingsController(
    std::shared_ptr<AuthService>         auth_service,
    std::shared_ptr<MySQLConnectionPool> pool,
    std::shared_ptr<AuditService>        audit_service)
    : auth_service_(std::move(auth_service)),
      pool_(std::move(pool)),
      audit_service_(std::move(audit_service)) {}

// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<User> SettingsController::AuthenticateAdmin(
    const HttpRequest& request, HttpResponse& error_response) const {
  std::string token = request.Header("authorization");
  if (token.size() > 7 && token.substr(0, 7) == "Bearer ") {
    token = token.substr(7);
  }
  if (token.empty()) {
    auto it = request.query_params.find("token");
    if (it != request.query_params.end()) token = it->second;
  }
  std::string auth_error;
  auto opt_user = auth_service_->GetUserFromToken(token, auth_error);
  if (!opt_user) {
    error_response = HttpResponse::Json(401, ErrorJson("UNAUTHORIZED", "未登录或 token 无效"));
    return nullptr;
  }
  if (!opt_user->is_admin) {
    error_response = HttpResponse::Json(403, ErrorJson("FORBIDDEN", "需要管理员权限"));
    return nullptr;
  }
  return std::make_shared<User>(*opt_user);
}

// ─────────────────────────────────────────────────────────────────────────────

void SettingsController::EnsureDefaultSettings(MYSQL* conn) const {
  // 建表（如果不存在）
  const char* create_sql =
      "CREATE TABLE IF NOT EXISTS system_settings ("
      "  id          INT UNSIGNED  NOT NULL AUTO_INCREMENT,"
      "  `key`       VARCHAR(100)  NOT NULL,"
      "  value       VARCHAR(4000) NOT NULL DEFAULT '',"
      "  type        VARCHAR(20)   NOT NULL DEFAULT 'string',"
      "  label       VARCHAR(200)  NOT NULL DEFAULT '',"
      "  description VARCHAR(500)  NOT NULL DEFAULT '',"
      "  `group`     VARCHAR(50)   NOT NULL DEFAULT 'basic',"
      "  updated_by  BIGINT UNSIGNED NULL,"
      "  updated_at  TIMESTAMP     NOT NULL DEFAULT CURRENT_TIMESTAMP"
      "              ON UPDATE CURRENT_TIMESTAMP,"
      "  PRIMARY KEY (id),"
      "  UNIQUE KEY uk_key (`key`)"
      ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
  if (mysql_query(conn, create_sql) != 0) {
    Logger::Warn(std::string("system_settings 建表失败: ") + mysql_error(conn));
    return;
  }

  // 插入缺失的默认配置项（使用 INSERT IGNORE 避免覆盖已有值）
  for (std::size_t i = 0; i < kSettingCount; ++i) {
    const auto& m = kDefaultSettings[i];
    std::ostringstream q;
    q << "INSERT IGNORE INTO system_settings (`key`, value, type, label, description, `group`) VALUES ("
      << "'" << EscapeStr(conn, m.key)         << "',"
      << "'" << EscapeStr(conn, m.default_value) << "',"
      << "'" << EscapeStr(conn, m.type)         << "',"
      << "'" << EscapeStr(conn, m.label)        << "',"
      << "'" << EscapeStr(conn, m.description)  << "',"
      << "'" << EscapeStr(conn, m.group)        << "')";
    if (mysql_query(conn, q.str().c_str()) != 0) {
      Logger::Warn(std::string("插入默认配置项失败 [") + m.key + "]: " + mysql_error(conn));
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────

/** GET /api/admin/settings */
HttpResponse SettingsController::GetSettings(const HttpRequest& request) {
  HttpResponse err;
  auto admin = AuthenticateAdmin(request, err);
  if (!admin) return err;

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  EnsureDefaultSettings(conn);

  const char* sql =
      "SELECT `key`, value, type, label, description, `group`, "
      "updated_by, UNIX_TIMESTAMP(updated_at) "
      "FROM system_settings ORDER BY `group`, id";

  if (mysql_query(conn, sql) != 0) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", mysql_error(conn)));
  }
  MYSQL_RES* res = mysql_store_result(conn);
  if (!res) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "无结果集"));
  }

  nlohmann::json items = nlohmann::json::array();
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    nlohmann::json item;
    item["key"]         = row[0] ? row[0] : "";
    item["value"]       = row[1] ? row[1] : "";
    item["type"]        = row[2] ? row[2] : "string";
    item["label"]       = row[3] ? row[3] : "";
    item["description"] = row[4] ? row[4] : "";
    item["group"]       = row[5] ? row[5] : "basic";
    if (row[6]) {
      item["updatedBy"] = std::stoull(row[6]);
    } else {
      item["updatedBy"] = nullptr;
    }
    item["updatedAt"] = row[7] ? std::stoull(row[7]) : 0;
    items.push_back(std::move(item));
  }
  mysql_free_result(res);

  return HttpResponse::Json(200, {{"items", items}});
}

// ─────────────────────────────────────────────────────────────────────────────

/** PUT /api/admin/settings */
HttpResponse SettingsController::UpdateSettings(const HttpRequest& request) {
  HttpResponse err;
  auto admin = AuthenticateAdmin(request, err);
  if (!admin) return err;

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (const std::exception& ex) {
    return HttpResponse::Json(400, ErrorJson("INVALID_JSON", std::string("JSON 解析失败: ") + ex.what()));
  }

  if (!body.is_object() || body.empty()) {
    return HttpResponse::Json(400, ErrorJson("INVALID_BODY", "请求体必须是包含配置键值对的 JSON 对象"));
  }

  auto conn_guard = pool_->GetConnection();
  MYSQL* conn = conn_guard.Get();
  if (!conn) {
    return HttpResponse::Json(500, ErrorJson("DB_ERROR", "数据库连接失败"));
  }

  EnsureDefaultSettings(conn);

  // 允许更新的键集合（白名单，防止任意键写入）
  std::vector<std::string> allowed_keys;
  allowed_keys.reserve(kSettingCount);
  for (std::size_t i = 0; i < kSettingCount; ++i) {
    allowed_keys.push_back(kDefaultSettings[i].key);
  }

  int updated = 0;
  int skipped = 0;
  nlohmann::json detail_log = nlohmann::json::object();

  for (auto& [key, val] : body.items()) {
    // 白名单校验
    bool allowed = false;
    for (const auto& ak : allowed_keys) {
      if (ak == key) { allowed = true; break; }
    }
    if (!allowed) {
      ++skipped;
      continue;
    }

    // 将值统一转为字符串存储
    std::string str_val;
    if (val.is_string()) {
      str_val = val.get<std::string>();
    } else if (val.is_boolean()) {
      str_val = val.get<bool>() ? "true" : "false";
    } else if (val.is_number_integer()) {
      str_val = std::to_string(val.get<long long>());
    } else if (val.is_number_float()) {
      str_val = std::to_string(val.get<double>());
    } else {
      str_val = val.dump();
    }

    // 长度限制
    if (str_val.size() > 4000) {
      str_val.resize(4000);
    }

    std::ostringstream q;
    q << "UPDATE system_settings SET value='"
      << EscapeStr(conn, str_val)
      << "', updated_by=" << admin->id
      << " WHERE `key`='" << EscapeStr(conn, key) << "'";

    if (mysql_query(conn, q.str().c_str()) == 0 && mysql_affected_rows(conn) > 0) {
      ++updated;
      detail_log[key] = str_val;
    } else if (mysql_errno(conn) != 0) {
      Logger::Warn(std::string("更新配置项失败 [") + key + "]: " + mysql_error(conn));
    }
  }

  // 写审计日志
  if (audit_service_ && updated > 0) {
    try {
      audit_service_->Write(admin->id, admin->username, "update_settings",
                            "setting", "batch",
                            detail_log.dump(), "");
    } catch (...) {}
  }

  Logger::Info("Admin " + admin->username + " updated " + std::to_string(updated) +
               " settings, skipped " + std::to_string(skipped));

  return HttpResponse::Json(200, {
    {"updated", updated},
    {"skipped", skipped},
    {"message", "配置已保存"}
  });
}
