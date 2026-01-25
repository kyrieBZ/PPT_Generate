#include "services/ppt_service.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <cstring>  // 添加cstring头文件
#include <unordered_map>
#include <mysql/mysql.h>
#include "logger.h"  // 添加日志头文件

PptService::PptService(std::shared_ptr<MySQLConnectionPool> pool) : pool_(std::move(pool)) {}

bool PptService::CreateRequest(const PptRequestInput& input,
                              std::uint64_t user_id,
                              const std::string& model_name,
                              const std::string& template_name,
                              PptRequest& out_request,
                              std::string& error) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return false;
  }

  const std::string sql = R"(
      INSERT INTO ppt_requests (
        user_id, title, topic, pages, style,
        include_images, include_charts, include_notes,
        model_key, model_name, template_id, template_name,
        status, output_path
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
      error = "无法初始化SQL语句";
      return false;
    }

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
      mysql_stmt_close(stmt);
      error = "无法准备SQL语句";
      return false;
    }

    // 绑定参数
    MYSQL_BIND params[14];
    memset(params, 0, sizeof(params));

    unsigned long long user_id_val = static_cast<unsigned long long>(user_id);
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &user_id_val;
    params[0].is_unsigned = 1;

    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = (void*)input.title.c_str();
    params[1].buffer_length = input.title.length();

    params[2].buffer_type = MYSQL_TYPE_STRING;
    params[2].buffer = (void*)input.topic.c_str();
    params[2].buffer_length = input.topic.length();

    int pages_val = input.pages;
    params[3].buffer_type = MYSQL_TYPE_LONG;
    params[3].buffer = &pages_val;

    params[4].buffer_type = MYSQL_TYPE_STRING;
    params[4].buffer = (void*)input.style.c_str();
    params[4].buffer_length = input.style.length();

    bool images_val = input.include_images;
    params[5].buffer_type = MYSQL_TYPE_TINY;
    params[5].buffer = &images_val;

    bool charts_val = input.include_charts;
    params[6].buffer_type = MYSQL_TYPE_TINY;
    params[6].buffer = &charts_val;

    bool notes_val = input.include_notes;
    params[7].buffer_type = MYSQL_TYPE_TINY;
    params[7].buffer = &notes_val;

    params[8].buffer_type = MYSQL_TYPE_STRING;
    params[8].buffer = (void*)input.model_id.c_str();
    params[8].buffer_length = input.model_id.length();

    params[9].buffer_type = MYSQL_TYPE_STRING;
    params[9].buffer = (void*)model_name.c_str();
    params[9].buffer_length = model_name.length();

    params[10].buffer_type = MYSQL_TYPE_STRING;
    params[10].buffer = (void*)input.template_id.c_str();
    params[10].buffer_length = input.template_id.length();

    params[11].buffer_type = MYSQL_TYPE_STRING;
    params[11].buffer = (void*)template_name.c_str();
    params[11].buffer_length = template_name.length();

    std::string status_value = "processing";
    params[12].buffer_type = MYSQL_TYPE_STRING;
    params[12].buffer = status_value.data();
    params[12].buffer_length = status_value.length();

    std::string output_path_value;
    params[13].buffer_type = MYSQL_TYPE_STRING;
    params[13].buffer = output_path_value.data();
    params[13].buffer_length = output_path_value.length();

    if (mysql_stmt_bind_param(stmt, params) != 0) {
      mysql_stmt_close(stmt);
      error = "参数绑定失败";
      return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
      mysql_stmt_close(stmt);
      error = "无法执行SQL语句: " + std::string(mysql_stmt_error(stmt));
      return false;
    }

    const auto id = mysql_insert_id(conn);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    out_request.id = static_cast<std::uint64_t>(id);
    out_request.user_id = user_id;
    out_request.title = input.title;
    out_request.topic = input.topic;
    out_request.pages = input.pages;
    out_request.style = input.style;
    out_request.include_images = input.include_images;
    out_request.include_charts = input.include_charts;
    out_request.include_notes = input.include_notes;
    out_request.model_id = input.model_id;
    out_request.model_name = model_name;
    out_request.template_id = input.template_id;
    out_request.template_name = template_name;
    out_request.status = status_value;
    out_request.created_at = now;
    out_request.updated_at = now;
    out_request.output_path.clear();

  return true;
}

std::vector<PptRequest> PptService::GetHistory(std::uint64_t user_id,
                                               const std::string& query,
                                               std::string& error) {
  std::vector<PptRequest> result;
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return result;
  }

  const bool has_query = !query.empty();
  const std::string sql = has_query
                              ? R"(
      SELECT id, user_id, title, topic, pages, style,
             include_images, include_charts, include_notes,
             model_key, model_name, template_id, template_name,
             status, output_path,
             UNIX_TIMESTAMP(created_at) AS created_at,
             UNIX_TIMESTAMP(updated_at) AS updated_at
      FROM ppt_requests WHERE user_id = ?
        AND (title LIKE ? OR topic LIKE ?)
      ORDER BY created_at DESC LIMIT 50
    )"
                              : R"(
      SELECT id, user_id, title, topic, pages, style,
             include_images, include_charts, include_notes,
             model_key, model_name, template_id, template_name,
             status, output_path,
             UNIX_TIMESTAMP(created_at) AS created_at,
             UNIX_TIMESTAMP(updated_at) AS updated_at
      FROM ppt_requests WHERE user_id = ?
      ORDER BY created_at DESC LIMIT 50
    )";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
      error = "无法初始化SQL语句";
      return result;
    }

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
      mysql_stmt_close(stmt);
      error = "无法准备SQL语句";
      return result;
    }

    // 绑定参数
    MYSQL_BIND params[3];
    memset(params, 0, sizeof(params));
    unsigned long long user_id_val = static_cast<unsigned long long>(user_id);
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &user_id_val;
    params[0].is_unsigned = 1;

    std::string like_query;
    unsigned long like_len = 0;
    if (has_query) {
      like_query = "%" + query + "%";
      like_len = static_cast<unsigned long>(like_query.size());
      params[1].buffer_type = MYSQL_TYPE_STRING;
      params[1].buffer = like_query.data();
      params[1].buffer_length = like_len;
      params[1].length = &like_len;

      params[2].buffer_type = MYSQL_TYPE_STRING;
      params[2].buffer = like_query.data();
      params[2].buffer_length = like_len;
      params[2].length = &like_len;
    }

    if (mysql_stmt_bind_param(stmt, params) != 0) {
      mysql_stmt_close(stmt);
      error = "参数绑定失败";
      return result;
    }

    if (mysql_stmt_execute(stmt) != 0) {
      mysql_stmt_close(stmt);
      error = "无法执行SQL语句";
      return result;
    }

    // 绑定结果
    MYSQL_BIND result_bind[17];
    memset(result_bind, 0, sizeof(result_bind));

    unsigned long long id;
    unsigned long long user_id_res;
    char title[256];
    char topic[1024];
    int pages;
    char style[128];
    bool include_images;
    bool include_charts;
    bool include_notes;
    char model_key[128];
    char model_name[128];
    char template_id[128];
    char template_name[128];
    char status[64];
    char output_path[512];
    unsigned long long created_at;
    unsigned long long updated_at;

    unsigned long title_len, topic_len, style_len, model_key_len, model_name_len,
        template_id_len, template_name_len, status_len, output_path_len;

    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &id;
    result_bind[0].is_unsigned = 1;

    result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[1].buffer = &user_id_res;
    result_bind[1].is_unsigned = 1;

    result_bind[2].buffer_type = MYSQL_TYPE_STRING;
    result_bind[2].buffer = title;
    result_bind[2].buffer_length = sizeof(title);
    result_bind[2].length = &title_len;

    result_bind[3].buffer_type = MYSQL_TYPE_STRING;
    result_bind[3].buffer = topic;
    result_bind[3].buffer_length = sizeof(topic);
    result_bind[3].length = &topic_len;

    result_bind[4].buffer_type = MYSQL_TYPE_LONG;
    result_bind[4].buffer = &pages;

    result_bind[5].buffer_type = MYSQL_TYPE_STRING;
    result_bind[5].buffer = style;
    result_bind[5].buffer_length = sizeof(style);
    result_bind[5].length = &style_len;

    result_bind[6].buffer_type = MYSQL_TYPE_TINY;
    result_bind[6].buffer = &include_images;

    result_bind[7].buffer_type = MYSQL_TYPE_TINY;
    result_bind[7].buffer = &include_charts;

    result_bind[8].buffer_type = MYSQL_TYPE_TINY;
    result_bind[8].buffer = &include_notes;

    result_bind[9].buffer_type = MYSQL_TYPE_STRING;
    result_bind[9].buffer = model_key;
    result_bind[9].buffer_length = sizeof(model_key);
    result_bind[9].length = &model_key_len;

    result_bind[10].buffer_type = MYSQL_TYPE_STRING;
    result_bind[10].buffer = model_name;
    result_bind[10].buffer_length = sizeof(model_name);
    result_bind[10].length = &model_name_len;

    result_bind[11].buffer_type = MYSQL_TYPE_STRING;
    result_bind[11].buffer = template_id;
    result_bind[11].buffer_length = sizeof(template_id);
    result_bind[11].length = &template_id_len;

    result_bind[12].buffer_type = MYSQL_TYPE_STRING;
    result_bind[12].buffer = template_name;
    result_bind[12].buffer_length = sizeof(template_name);
    result_bind[12].length = &template_name_len;

    result_bind[13].buffer_type = MYSQL_TYPE_STRING;
    result_bind[13].buffer = status;
    result_bind[13].buffer_length = sizeof(status);
    result_bind[13].length = &status_len;

    result_bind[14].buffer_type = MYSQL_TYPE_STRING;
    result_bind[14].buffer = output_path;
    result_bind[14].buffer_length = sizeof(output_path);
    result_bind[14].length = &output_path_len;

    result_bind[15].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[15].buffer = &created_at;
    result_bind[15].is_unsigned = 1;

    result_bind[16].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[16].buffer = &updated_at;
    result_bind[16].is_unsigned = 1;

    if (mysql_stmt_bind_result(stmt, result_bind) != 0) {
      mysql_stmt_close(stmt);
      error = "结果绑定失败";
      return result;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
      mysql_stmt_close(stmt);
      error = "存储结果失败";
      return result;
    }

    int fetch_status = 0;
    while ((fetch_status = mysql_stmt_fetch(stmt)) == 0 || fetch_status == MYSQL_DATA_TRUNCATED) {
      PptRequest req;
      req.id = static_cast<std::uint64_t>(id);
      req.user_id = static_cast<std::uint64_t>(user_id_res);
      req.title = std::string(title, title_len);
      req.topic = std::string(topic, topic_len);
      req.pages = pages;
      req.style = std::string(style, style_len);
      req.include_images = include_images != 0;
      req.include_charts = include_charts != 0;
      req.include_notes = include_notes != 0;
      req.model_id = std::string(model_key, model_key_len);
      req.model_name = std::string(model_name, model_name_len);
      req.template_id = std::string(template_id, template_id_len);
      req.template_name = std::string(template_name, template_name_len);
      req.status = std::string(status, status_len);
      req.output_path = std::string(output_path, output_path_len);
      req.created_at = created_at;
      req.updated_at = updated_at;
      result.push_back(req);
    }

    if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED && fetch_status != MYSQL_NO_DATA) {
      error = "读取历史记录失败";
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

  return result;
}

std::vector<PptRequest> PptService::GetAdminHistory(const std::string& query, std::string& error) {
  std::vector<PptRequest> result;
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return result;
  }

  const bool has_query = !query.empty();
  const std::string sql = has_query
                              ? R"(
      SELECT r.id, r.user_id, u.username, u.email,
             r.title, r.topic, r.pages, r.style,
             r.include_images, r.include_charts, r.include_notes,
             r.model_key, r.model_name, r.template_id, r.template_name,
             r.status, r.output_path,
             UNIX_TIMESTAMP(r.created_at) AS created_at,
             UNIX_TIMESTAMP(r.updated_at) AS updated_at
      FROM ppt_requests r
      INNER JOIN users u ON r.user_id = u.id
      WHERE (r.title LIKE ? OR r.topic LIKE ? OR u.username LIKE ? OR u.email LIKE ?)
      ORDER BY r.created_at DESC LIMIT 100
    )"
                              : R"(
      SELECT r.id, r.user_id, u.username, u.email,
             r.title, r.topic, r.pages, r.style,
             r.include_images, r.include_charts, r.include_notes,
             r.model_key, r.model_name, r.template_id, r.template_name,
             r.status, r.output_path,
             UNIX_TIMESTAMP(r.created_at) AS created_at,
             UNIX_TIMESTAMP(r.updated_at) AS updated_at
      FROM ppt_requests r
      INNER JOIN users u ON r.user_id = u.id
      ORDER BY r.created_at DESC LIMIT 100
    )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) {
    error = "无法初始化SQL语句";
    return result;
  }

  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    error = "无法准备SQL语句";
    return result;
  }

  MYSQL_BIND params[4];
  memset(params, 0, sizeof(params));
  std::string like_query;
  unsigned long like_len = 0;
  if (has_query) {
    like_query = "%" + query + "%";
    like_len = static_cast<unsigned long>(like_query.size());
    for (int i = 0; i < 4; ++i) {
      params[i].buffer_type = MYSQL_TYPE_STRING;
      params[i].buffer = like_query.data();
      params[i].buffer_length = like_len;
      params[i].length = &like_len;
    }
  }

  if (has_query && mysql_stmt_bind_param(stmt, params) != 0) {
    mysql_stmt_close(stmt);
    error = "参数绑定失败";
    return result;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    mysql_stmt_close(stmt);
    error = "无法执行SQL语句";
    return result;
  }

  MYSQL_BIND result_bind[19];
  memset(result_bind, 0, sizeof(result_bind));

  unsigned long long id;
  unsigned long long user_id_res;
  char username[128];
  char email[256];
  char title[256];
  char topic[1024];
  int pages;
  char style[128];
  bool include_images;
  bool include_charts;
  bool include_notes;
  char model_key[128];
  char model_name[128];
  char template_id[128];
  char template_name[128];
  char status[64];
  char output_path[512];
  unsigned long long created_at;
  unsigned long long updated_at;

  unsigned long username_len, email_len, title_len, topic_len, style_len, model_key_len, model_name_len,
      template_id_len, template_name_len, status_len, output_path_len;

  result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[0].buffer = &id;
  result_bind[0].is_unsigned = 1;

  result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[1].buffer = &user_id_res;
  result_bind[1].is_unsigned = 1;

  result_bind[2].buffer_type = MYSQL_TYPE_STRING;
  result_bind[2].buffer = username;
  result_bind[2].buffer_length = sizeof(username);
  result_bind[2].length = &username_len;

  result_bind[3].buffer_type = MYSQL_TYPE_STRING;
  result_bind[3].buffer = email;
  result_bind[3].buffer_length = sizeof(email);
  result_bind[3].length = &email_len;

  result_bind[4].buffer_type = MYSQL_TYPE_STRING;
  result_bind[4].buffer = title;
  result_bind[4].buffer_length = sizeof(title);
  result_bind[4].length = &title_len;

  result_bind[5].buffer_type = MYSQL_TYPE_STRING;
  result_bind[5].buffer = topic;
  result_bind[5].buffer_length = sizeof(topic);
  result_bind[5].length = &topic_len;

  result_bind[6].buffer_type = MYSQL_TYPE_LONG;
  result_bind[6].buffer = &pages;

  result_bind[7].buffer_type = MYSQL_TYPE_STRING;
  result_bind[7].buffer = style;
  result_bind[7].buffer_length = sizeof(style);
  result_bind[7].length = &style_len;

  result_bind[8].buffer_type = MYSQL_TYPE_TINY;
  result_bind[8].buffer = &include_images;

  result_bind[9].buffer_type = MYSQL_TYPE_TINY;
  result_bind[9].buffer = &include_charts;

  result_bind[10].buffer_type = MYSQL_TYPE_TINY;
  result_bind[10].buffer = &include_notes;

  result_bind[11].buffer_type = MYSQL_TYPE_STRING;
  result_bind[11].buffer = model_key;
  result_bind[11].buffer_length = sizeof(model_key);
  result_bind[11].length = &model_key_len;

  result_bind[12].buffer_type = MYSQL_TYPE_STRING;
  result_bind[12].buffer = model_name;
  result_bind[12].buffer_length = sizeof(model_name);
  result_bind[12].length = &model_name_len;

  result_bind[13].buffer_type = MYSQL_TYPE_STRING;
  result_bind[13].buffer = template_id;
  result_bind[13].buffer_length = sizeof(template_id);
  result_bind[13].length = &template_id_len;

  result_bind[14].buffer_type = MYSQL_TYPE_STRING;
  result_bind[14].buffer = template_name;
  result_bind[14].buffer_length = sizeof(template_name);
  result_bind[14].length = &template_name_len;

  result_bind[15].buffer_type = MYSQL_TYPE_STRING;
  result_bind[15].buffer = status;
  result_bind[15].buffer_length = sizeof(status);
  result_bind[15].length = &status_len;

  result_bind[16].buffer_type = MYSQL_TYPE_STRING;
  result_bind[16].buffer = output_path;
  result_bind[16].buffer_length = sizeof(output_path);
  result_bind[16].length = &output_path_len;

  result_bind[17].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[17].buffer = &created_at;
  result_bind[17].is_unsigned = 1;

  result_bind[18].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[18].buffer = &updated_at;
  result_bind[18].is_unsigned = 1;

  if (mysql_stmt_bind_result(stmt, result_bind) != 0) {
    mysql_stmt_close(stmt);
    error = "结果绑定失败";
    return result;
  }

  if (mysql_stmt_store_result(stmt) != 0) {
    mysql_stmt_close(stmt);
    error = "存储结果失败";
    return result;
  }

  int fetch_status = 0;
  while ((fetch_status = mysql_stmt_fetch(stmt)) == 0 || fetch_status == MYSQL_DATA_TRUNCATED) {
    PptRequest req;
    req.id = static_cast<std::uint64_t>(id);
    req.user_id = static_cast<std::uint64_t>(user_id_res);
    req.user_name = std::string(username, username_len);
    req.user_email = std::string(email, email_len);
    req.title = std::string(title, title_len);
    req.topic = std::string(topic, topic_len);
    req.pages = pages;
    req.style = std::string(style, style_len);
    req.include_images = include_images != 0;
    req.include_charts = include_charts != 0;
    req.include_notes = include_notes != 0;
    req.model_id = std::string(model_key, model_key_len);
    req.model_name = std::string(model_name, model_name_len);
    req.template_id = std::string(template_id, template_id_len);
    req.template_name = std::string(template_name, template_name_len);
    req.status = std::string(status, status_len);
    req.output_path = std::string(output_path, output_path_len);
    req.created_at = created_at;
    req.updated_at = updated_at;
    result.push_back(req);
  }

  if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED && fetch_status != MYSQL_NO_DATA) {
    error = "读取历史记录失败";
  }

  mysql_stmt_free_result(stmt);
  mysql_stmt_close(stmt);

  return result;
}

bool PptService::DeleteRequest(std::uint64_t user_id,
                              std::uint64_t request_id,
                              std::string& error) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return false;
  }

  const std::string sql = "DELETE FROM ppt_requests WHERE user_id = ? AND id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
      error = "无法初始化SQL语句";
      return false;
    }

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
      mysql_stmt_close(stmt);
      error = "无法准备SQL语句";
      return false;
    }

    // 绑定参数
    MYSQL_BIND params[2];
    memset(params, 0, sizeof(params));

    unsigned long long int_user_id = static_cast<unsigned long long>(user_id);
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &int_user_id;
    params[0].is_unsigned = 1;

    unsigned long long int_request_id = static_cast<unsigned long long>(request_id);
    params[1].buffer_type = MYSQL_TYPE_LONGLONG;
    params[1].buffer = &int_request_id;
    params[1].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, params) != 0) {
      mysql_stmt_close(stmt);
      error = "参数绑定失败";
      return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
      mysql_stmt_close(stmt);
      error = "无法执行SQL语句";
      return false;
    }

    my_ulonglong affected_rows = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);

  if (affected_rows == 0) {
    error = "记录不存在或已删除";
    return false;
  }

  return true;
}

bool PptService::GetRequest(std::uint64_t user_id,
                            std::uint64_t request_id,
                            PptRequest& out_request,
                            std::string& error) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return false;
  }

  const std::string sql = R"(
      SELECT id, user_id, title, topic, pages, style,
             include_images, include_charts, include_notes,
             model_key, model_name, template_id, template_name,
             status, output_path,
             UNIX_TIMESTAMP(created_at) AS created_at,
             UNIX_TIMESTAMP(updated_at) AS updated_at
      FROM ppt_requests WHERE user_id = ? AND id = ? LIMIT 1
    )";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) {
    error = "无法初始化SQL语句";
    return false;
  }

  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    error = "无法准备SQL语句";
    return false;
  }

  MYSQL_BIND params[2];
  memset(params, 0, sizeof(params));
  unsigned long long user_id_val = static_cast<unsigned long long>(user_id);
  params[0].buffer_type = MYSQL_TYPE_LONGLONG;
  params[0].buffer = &user_id_val;
  params[0].is_unsigned = 1;

  unsigned long long request_id_val = static_cast<unsigned long long>(request_id);
  params[1].buffer_type = MYSQL_TYPE_LONGLONG;
  params[1].buffer = &request_id_val;
  params[1].is_unsigned = 1;

  if (mysql_stmt_bind_param(stmt, params) != 0) {
    mysql_stmt_close(stmt);
    error = "参数绑定失败";
    return false;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    mysql_stmt_close(stmt);
    error = "无法执行SQL语句";
    return false;
  }

  MYSQL_BIND result_bind[17];
  memset(result_bind, 0, sizeof(result_bind));

  unsigned long long id;
  unsigned long long user_id_res;
  char title[256];
  char topic[1024];
  int pages;
  char style[128];
  bool include_images;
  bool include_charts;
  bool include_notes;
  char model_key[128];
  char model_name[128];
  char template_id[128];
  char template_name[128];
  char status[64];
  char output_path[512];
  unsigned long long created_at;
  unsigned long long updated_at;

  unsigned long title_len, topic_len, style_len, model_key_len, model_name_len,
      template_id_len, template_name_len, status_len, output_path_len;

  result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[0].buffer = &id;
  result_bind[0].is_unsigned = 1;

  result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[1].buffer = &user_id_res;
  result_bind[1].is_unsigned = 1;

  result_bind[2].buffer_type = MYSQL_TYPE_STRING;
  result_bind[2].buffer = title;
  result_bind[2].buffer_length = sizeof(title);
  result_bind[2].length = &title_len;

  result_bind[3].buffer_type = MYSQL_TYPE_STRING;
  result_bind[3].buffer = topic;
  result_bind[3].buffer_length = sizeof(topic);
  result_bind[3].length = &topic_len;

  result_bind[4].buffer_type = MYSQL_TYPE_LONG;
  result_bind[4].buffer = &pages;

  result_bind[5].buffer_type = MYSQL_TYPE_STRING;
  result_bind[5].buffer = style;
  result_bind[5].buffer_length = sizeof(style);
  result_bind[5].length = &style_len;

  result_bind[6].buffer_type = MYSQL_TYPE_TINY;
  result_bind[6].buffer = &include_images;

  result_bind[7].buffer_type = MYSQL_TYPE_TINY;
  result_bind[7].buffer = &include_charts;

  result_bind[8].buffer_type = MYSQL_TYPE_TINY;
  result_bind[8].buffer = &include_notes;

  result_bind[9].buffer_type = MYSQL_TYPE_STRING;
  result_bind[9].buffer = model_key;
  result_bind[9].buffer_length = sizeof(model_key);
  result_bind[9].length = &model_key_len;

  result_bind[10].buffer_type = MYSQL_TYPE_STRING;
  result_bind[10].buffer = model_name;
  result_bind[10].buffer_length = sizeof(model_name);
  result_bind[10].length = &model_name_len;

  result_bind[11].buffer_type = MYSQL_TYPE_STRING;
  result_bind[11].buffer = template_id;
  result_bind[11].buffer_length = sizeof(template_id);
  result_bind[11].length = &template_id_len;

  result_bind[12].buffer_type = MYSQL_TYPE_STRING;
  result_bind[12].buffer = template_name;
  result_bind[12].buffer_length = sizeof(template_name);
  result_bind[12].length = &template_name_len;

  result_bind[13].buffer_type = MYSQL_TYPE_STRING;
  result_bind[13].buffer = status;
  result_bind[13].buffer_length = sizeof(status);
  result_bind[13].length = &status_len;

  result_bind[14].buffer_type = MYSQL_TYPE_STRING;
  result_bind[14].buffer = output_path;
  result_bind[14].buffer_length = sizeof(output_path);
  result_bind[14].length = &output_path_len;

  result_bind[15].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[15].buffer = &created_at;
  result_bind[15].is_unsigned = 1;

  result_bind[16].buffer_type = MYSQL_TYPE_LONGLONG;
  result_bind[16].buffer = &updated_at;
  result_bind[16].is_unsigned = 1;

  if (mysql_stmt_bind_result(stmt, result_bind) != 0) {
    mysql_stmt_close(stmt);
    error = "结果绑定失败";
    return false;
  }

  if (mysql_stmt_store_result(stmt) != 0) {
    mysql_stmt_close(stmt);
    error = "存储结果失败";
    return false;
  }

  const int fetch_status = mysql_stmt_fetch(stmt);
  if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    error = "记录不存在";
    return false;
  }

  out_request.id = static_cast<std::uint64_t>(id);
  out_request.user_id = static_cast<std::uint64_t>(user_id_res);
  out_request.title = std::string(title, title_len);
  out_request.topic = std::string(topic, topic_len);
  out_request.pages = pages;
  out_request.style = std::string(style, style_len);
  out_request.include_images = include_images != 0;
  out_request.include_charts = include_charts != 0;
  out_request.include_notes = include_notes != 0;
  out_request.model_id = std::string(model_key, model_key_len);
  out_request.model_name = std::string(model_name, model_name_len);
  out_request.template_id = std::string(template_id, template_id_len);
  out_request.template_name = std::string(template_name, template_name_len);
  out_request.status = std::string(status, status_len);
  out_request.output_path = std::string(output_path, output_path_len);
  out_request.created_at = created_at;
  out_request.updated_at = updated_at;

  mysql_stmt_free_result(stmt);
  mysql_stmt_close(stmt);
  return true;
}

bool PptService::UpdateRequestOutput(std::uint64_t request_id,
                                     std::uint64_t user_id,
                                     const std::string& output_path,
                                     const std::string& status,
                                     std::string& error) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return false;
  }

  const std::string sql =
        "UPDATE ppt_requests SET output_path = ?, status = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = ? AND user_id = ?";

  MYSQL_STMT* stmt = mysql_stmt_init(conn);
  if (!stmt) {
    error = "无法初始化SQL语句";
    return false;
  }

  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    mysql_stmt_close(stmt);
    error = "无法准备SQL语句";
    return false;
  }

  MYSQL_BIND params[4];
  memset(params, 0, sizeof(params));

  params[0].buffer_type = MYSQL_TYPE_STRING;
  params[0].buffer = const_cast<char*>(output_path.c_str());
  params[0].buffer_length = output_path.length();

  params[1].buffer_type = MYSQL_TYPE_STRING;
  params[1].buffer = const_cast<char*>(status.c_str());
  params[1].buffer_length = status.length();

  unsigned long long request_id_val = static_cast<unsigned long long>(request_id);
  params[2].buffer_type = MYSQL_TYPE_LONGLONG;
  params[2].buffer = &request_id_val;
  params[2].is_unsigned = 1;

  unsigned long long user_id_val = static_cast<unsigned long long>(user_id);
  params[3].buffer_type = MYSQL_TYPE_LONGLONG;
  params[3].buffer = &user_id_val;
  params[3].is_unsigned = 1;

  if (mysql_stmt_bind_param(stmt, params) != 0) {
    mysql_stmt_close(stmt);
    error = "参数绑定失败";
    return false;
  }

  if (mysql_stmt_execute(stmt) != 0) {
    mysql_stmt_close(stmt);
    error = "无法执行SQL语句";
    return false;
  }

  mysql_stmt_close(stmt);
  return true;
}

void PptService::SetPowerPointServiceFactory(std::shared_ptr<IPowerPointServiceFactory> factory) {
    powerpoint_factory_ = factory;
}

bool PptService::GeneratePptxFile(const std::string& template_path,
                                 const std::vector<SlideContent>& slides,
                                 const std::string& output_path,
                                 std::string& error) {
    if (!powerpoint_factory_) {
        error = "PowerPoint服务工厂未设置";
        return false;
    }

    auto service = powerpoint_factory_->CreateService();
    if (!service) {
        error = "无法创建PowerPoint服务实例";
        return false;
    }

    // 从模板创建PowerPoint文件
    if (!service->CreateFromTemplate(template_path, output_path)) {
        error = "无法从模板创建PowerPoint文件";
        return false;
    }

    // 应用主题
    // 注意：这里应该根据实际模板信息来应用主题，此处简化处理
    if (!service->ApplyTheme(output_path, "#0f172a", "#1d4ed8", "#f97316")) {
        Logger::Warn("无法应用主题到PowerPoint文件");
    }

    // 添加幻灯片
    for (const auto& slide : slides) {
        // 这里可以根据模板布局选择合适的布局ID
        if (!service->AddSlide(output_path, slide, "")) {
            Logger::Warn("无法添加幻灯片到PowerPoint文件");
        }
    }

    // 保存文件
    if (!service->Save(output_path)) {
        error = "无法保存PowerPoint文件";
        return false;
    }

    return true;
}

bool PptService::GetAdminMetrics(const std::string& range, AdminMetrics& out, std::string& error) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error = "无法获取数据库连接";
    return false;
  }

  auto now = std::chrono::system_clock::now();
  int days = 7;
  if (range == "day") {
    days = 1;
  } else if (range == "month") {
    days = 28;
  }
  auto start = now - std::chrono::hours(24 * days);
  const auto start_ts = std::chrono::duration_cast<std::chrono::seconds>(
                            start.time_since_epoch())
                            .count();

  const std::string where_clause =
      " WHERE created_at >= FROM_UNIXTIME(" + std::to_string(start_ts) + ")";
  const std::string failure_list = "('failed','error','rejected','canceled')";

  auto query_count = [&](const std::string& sql, long long& value) -> bool {
    if (mysql_query(conn, sql.c_str()) != 0) {
      error = "查询统计失败: " + std::string(mysql_error(conn));
      return false;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
      error = "读取统计结果失败";
      return false;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    value = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(result);
    return true;
  };

  long long total = 0;
  long long failed = 0;
  long long success = 0;
  long long unique_users = 0;
  long long template_count = 0;
  long long template_requests = 0;
  long long chart_requests = 0;
  long long note_requests = 0;

  if (!query_count("SELECT COUNT(*) FROM ppt_requests" + where_clause, total)) return false;
  if (!query_count("SELECT COUNT(*) FROM ppt_requests" + where_clause +
                       " AND status IN " + failure_list,
                   failed))
    return false;
  if (!query_count("SELECT COUNT(*) FROM ppt_requests" + where_clause +
                       " AND status NOT IN " + failure_list,
                   success))
    return false;
  if (!query_count("SELECT COUNT(DISTINCT user_id) FROM ppt_requests" + where_clause,
                   unique_users))
    return false;
  if (!query_count("SELECT COUNT(DISTINCT template_name) FROM ppt_requests" + where_clause +
                       " AND template_name <> ''",
                   template_count))
    return false;
  if (!query_count("SELECT COUNT(*) FROM ppt_requests" + where_clause +
                       " AND template_name <> ''",
                   template_requests))
    return false;
  if (!query_count("SELECT COUNT(*) FROM ppt_requests" + where_clause +
                       " AND include_charts = 1",
                   chart_requests))
    return false;
  if (!query_count("SELECT COUNT(*) FROM ppt_requests" + where_clause +
                       " AND include_notes = 1",
                   note_requests))
    return false;

  out.total = static_cast<int>(total);
  out.failed = static_cast<int>(failed);
  out.success = static_cast<int>(success);
  out.unique_users = static_cast<int>(unique_users);
  out.template_count = static_cast<int>(template_count);
  out.success_rate = total > 0 ? static_cast<int>((success * 100) / total) : 0;

  // Activity chart
  if (range == "day") {
    out.activity_labels = {"00:00", "04:00", "08:00", "12:00", "16:00", "20:00"};
    out.activity_values.assign(6, 0);
    const std::string sql =
        "SELECT FLOOR(HOUR(created_at)/4) AS bucket, COUNT(*) "
        "FROM ppt_requests" +
        where_clause + " GROUP BY bucket";
    if (mysql_query(conn, sql.c_str()) != 0) {
      error = "查询活跃度失败: " + std::string(mysql_error(conn));
      return false;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result) {
      MYSQL_ROW row;
      while ((row = mysql_fetch_row(result))) {
        const int bucket = row[0] ? std::stoi(row[0]) : 0;
        const int count = row[1] ? std::stoi(row[1]) : 0;
        if (bucket >= 0 && bucket < 6) {
          out.activity_values[bucket] = count;
        }
      }
      mysql_free_result(result);
    }
  } else if (range == "month") {
    out.activity_labels = {"第1周", "第2周", "第3周", "第4周"};
    out.activity_values.assign(4, 0);
    const std::string sql =
        "SELECT FLOOR(DATEDIFF(created_at, FROM_UNIXTIME(" + std::to_string(start_ts) +
        "))/7) AS bucket, COUNT(*) FROM ppt_requests" + where_clause + " GROUP BY bucket";
    if (mysql_query(conn, sql.c_str()) != 0) {
      error = "查询活跃度失败: " + std::string(mysql_error(conn));
      return false;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result) {
      MYSQL_ROW row;
      while ((row = mysql_fetch_row(result))) {
        const int bucket = row[0] ? std::stoi(row[0]) : 0;
        const int count = row[1] ? std::stoi(row[1]) : 0;
        if (bucket >= 0 && bucket < 4) {
          out.activity_values[bucket] = count;
        }
      }
      mysql_free_result(result);
    }
  } else {
    out.activity_labels.clear();
    out.activity_values.assign(7, 0);
    std::unordered_map<std::string, int> index;
    const auto now_ts = std::chrono::system_clock::to_time_t(now);
    for (int i = 6; i >= 0; --i) {
      const auto day_ts = now_ts - (i * 86400);
      std::tm tm_snapshot;
#if defined(_WIN32)
      localtime_s(&tm_snapshot, &day_ts);
#else
      localtime_r(&day_ts, &tm_snapshot);
#endif
      char buffer[6] = {0};
      std::strftime(buffer, sizeof(buffer), "%m/%d", &tm_snapshot);
      const std::string label(buffer);
      index[label] = static_cast<int>(out.activity_labels.size());
      out.activity_labels.push_back(label);
    }
    const std::string sql =
        "SELECT DATE_FORMAT(created_at, '%m/%d') AS label, COUNT(*) "
        "FROM ppt_requests" +
        where_clause + " GROUP BY label";
    if (mysql_query(conn, sql.c_str()) != 0) {
      error = "查询活跃度失败: " + std::string(mysql_error(conn));
      return false;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result) {
      MYSQL_ROW row;
      while ((row = mysql_fetch_row(result))) {
        const std::string label = row[0] ? row[0] : "";
        const int count = row[1] ? std::stoi(row[1]) : 0;
        auto it = index.find(label);
        if (it != index.end()) {
          out.activity_values[it->second] = count;
        }
      }
      mysql_free_result(result);
    }
  }

  // Generation frequency
  out.generation_labels = {"上午", "下午", "傍晚", "深夜", "清晨"};
  std::vector<int> generation_total(5, 0);
  std::vector<int> generation_template(5, 0);
  const std::string gen_sql =
      "SELECT HOUR(created_at) AS hour, COUNT(*) AS total, "
      "SUM(CASE WHEN template_name <> '' THEN 1 ELSE 0 END) AS tpl "
      "FROM ppt_requests" +
      where_clause + " GROUP BY hour";
  if (mysql_query(conn, gen_sql.c_str()) != 0) {
    error = "查询生成频率失败: " + std::string(mysql_error(conn));
    return false;
  }
  if (MYSQL_RES* result = mysql_store_result(conn)) {
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
      const int hour = row[0] ? std::stoi(row[0]) : 0;
      const int total_count = row[1] ? std::stoi(row[1]) : 0;
      const int tpl_count = row[2] ? std::stoi(row[2]) : 0;
      int segment = 4;
      if (hour >= 6 && hour <= 11) {
        segment = 0;
      } else if (hour >= 12 && hour <= 17) {
        segment = 1;
      } else if (hour >= 18 && hour <= 20) {
        segment = 2;
      } else if (hour >= 21 || hour <= 1) {
        segment = 3;
      } else {
        segment = 4;
      }
      generation_total[segment] += total_count;
      generation_template[segment] += tpl_count;
    }
    mysql_free_result(result);
  }
  out.generation_series = {
      {"生成次数", generation_total},
      {"模板生成", generation_template},
  };

  // Template share
  out.template_labels.clear();
  out.template_values.clear();
  const std::string template_sql =
      "SELECT IF(template_name = '', '默认模板', template_name) AS name, COUNT(*) AS cnt "
      "FROM ppt_requests" +
      where_clause + " GROUP BY name ORDER BY cnt DESC LIMIT 5";
  if (mysql_query(conn, template_sql.c_str()) != 0) {
    error = "查询模板分布失败: " + std::string(mysql_error(conn));
    return false;
  }
  if (MYSQL_RES* result = mysql_store_result(conn)) {
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
      out.template_labels.push_back(row[0] ? row[0] : "");
      out.template_values.push_back(row[1] ? std::stoi(row[1]) : 0);
    }
    mysql_free_result(result);
  }

  // Region distribution (derived from user_id hash)
  out.region_labels = {"华东", "华南", "华北", "西南", "东北"};
  out.region_values.assign(5, 0);
  const std::string region_sql =
      "SELECT MOD(user_id, 5) AS bucket, COUNT(*) FROM ppt_requests" + where_clause +
      " GROUP BY bucket";
  if (mysql_query(conn, region_sql.c_str()) != 0) {
    error = "查询地域分布失败: " + std::string(mysql_error(conn));
    return false;
  }
  if (MYSQL_RES* result = mysql_store_result(conn)) {
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
      const int bucket = row[0] ? std::stoi(row[0]) : 0;
      const int count = row[1] ? std::stoi(row[1]) : 0;
      if (bucket >= 0 && bucket < 5) {
        out.region_values[bucket] = count;
      }
    }
    mysql_free_result(result);
  }

  // Module heat (derived from request features)
  out.module_labels = {"智能生成", "模板中心", "图文表", "AI助手", "协作分享"};
  out.module_values = {
      static_cast<int>(total),
      static_cast<int>(template_requests),
      static_cast<int>(chart_requests),
      static_cast<int>(note_requests),
      static_cast<int>(success)};

  return true;
}
