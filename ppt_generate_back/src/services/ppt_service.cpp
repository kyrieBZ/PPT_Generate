#include "services/ppt_service.h"

#include <chrono>
#include <sstream>
#include <cstring>  // 添加cstring头文件
#include <mysql/mysql.h>
#include "logger.h"  // 添加日志头文件

PptService::PptService(std::shared_ptr<MySQLConnectionPool> pool) : pool_(std::move(pool)) {}

bool PptService::CreateRequest(const PptRequestInput& input,
                              std::uint64_t user_id,
                              const std::string& model_name,
                              const std::string& template_name,
                              PptRequest& out_request,
                              std::string& error) {
  if (auto conn = pool_->Acquire(); conn) {
    const std::string sql = R"(
      INSERT INTO ppt_requests (
        user_id, title, topic, pages, style,
        include_images, include_charts, include_notes,
        model_key, model_name, template_id, template_name,
        status
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
    MYSQL_BIND params[13];
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
    out_request.output_file.clear();

    return true;
  } else {
    error = "无法获取数据库连接";
    return false;
  }
}

std::vector<PptRequest> PptService::GetHistory(std::uint64_t user_id, std::string& error) {
  std::vector<PptRequest> result;
  if (auto conn = pool_->Acquire(); conn) {
    const std::string sql = R"(
      SELECT id, user_id, title, topic, pages, style,
             include_images, include_charts, include_notes,
             model_key, model_name, template_id, template_name,
             status, created_at, updated_at
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
    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    unsigned long long user_id_val = static_cast<unsigned long long>(user_id);
    param.buffer_type = MYSQL_TYPE_LONGLONG;
    param.buffer = &user_id_val;
    param.is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, &param) != 0) {
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
    MYSQL_BIND result_bind[16];
    memset(result_bind, 0, sizeof(result_bind));

    unsigned long id;
    unsigned long long user_id_res;
    char title[256];
    char topic[256];
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
    unsigned long long created_at;
    unsigned long long updated_at;

    unsigned long title_len, topic_len, style_len, model_key_len, model_name_len,
        template_id_len, template_name_len, status_len;

    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
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

    result_bind[14].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[14].buffer = &created_at;
    result_bind[14].is_unsigned = 1;

    result_bind[15].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[15].buffer = &updated_at;
    result_bind[15].is_unsigned = 1;

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

    while (mysql_stmt_fetch(stmt) == 0) {
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
      req.created_at = created_at;
      req.updated_at = updated_at;
      req.output_file.clear();
      result.push_back(req);
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
  } else {
    error = "无法获取数据库连接";
  }

  return result;
}

bool PptService::DeleteRequest(std::uint64_t user_id,
                              std::uint64_t request_id,
                              std::string& error) {
  if (auto conn = pool_->Acquire(); conn) {
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

    return affected_rows > 0;
  } else {
    error = "无法获取数据库连接";
    return false;
  }
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
