#include <cstdlib>
#include "services/ppt_service.h"

#include <sstream>

#include "logger.h"

namespace {
std::string EscapeString(MYSQL* connection, const std::string& value) {
  std::string escaped;
  escaped.resize(value.size() * 2 + 1);
  const auto length = mysql_real_escape_string(connection, escaped.data(), value.c_str(), value.length());
  escaped.resize(length);
  return escaped;
}

PptRequest RowToRequest(MYSQL_ROW row) {
  PptRequest request;
  request.id = row[0] ? std::strtoull(row[0], nullptr, 10) : 0;
  request.user_id = row[1] ? std::strtoull(row[1], nullptr, 10) : 0;
  request.title = row[2] ? row[2] : "";
  request.topic = row[3] ? row[3] : "";
  request.pages = row[4] ? std::atoi(row[4]) : 0;
  request.style = row[5] ? row[5] : "";
  request.include_images = row[6] ? std::atoi(row[6]) != 0 : false;
  request.include_charts = row[7] ? std::atoi(row[7]) != 0 : false;
  request.include_notes = row[8] ? std::atoi(row[8]) != 0 : false;
  request.model_id = row[9] ? row[9] : "";
  request.model_name = row[10] ? row[10] : "";
  request.template_id = row[11] ? row[11] : "";
  request.template_name = row[12] ? row[12] : "";
  request.status = row[13] ? row[13] : "";
  request.created_at = row[14] ? row[14] : "";
  request.updated_at = row[15] ? row[15] : "";
  return request;
}
}

PptService::PptService(std::shared_ptr<MySQLConnectionPool> pool) : pool_(std::move(pool)) {}

bool PptService::CreateRequest(const PptRequestInput& input,
                               std::uint64_t user_id,
                               const std::string& model_name,
                               const std::string& template_name,
                               PptRequest& out_request,
                               std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();

  const auto title = EscapeString(conn, input.title);
  const auto topic = EscapeString(conn, input.topic);
  const auto style = EscapeString(conn, input.style);
  const auto template_id = EscapeString(conn, input.template_id);
  const auto tpl_name = EscapeString(conn, template_name);

  std::ostringstream query;
  query << "INSERT INTO ppt_requests (user_id, title, topic, pages, style, include_images, include_charts, include_notes, model_key, model_name, template_id, template_name, status) "
        << "VALUES (" << user_id << ", '" << title << "', '" << topic << "', " << input.pages << ", '" << style
        << "', " << (input.include_images ? 1 : 0) << ", " << (input.include_charts ? 1 : 0) << ", "
        << (input.include_notes ? 1 : 0) << ", '" << EscapeString(conn, input.model_id) << "', '"
        << EscapeString(conn, model_name) << "', '" << template_id << "', '" << tpl_name << "', 'completed')";

  if (mysql_query(conn, query.str().c_str()) != 0) {
    error_message = mysql_error(conn);
    Logger::Error("保存PPT请求失败: " + error_message);
    return false;
  }

  const auto request_id = mysql_insert_id(conn);
  const std::string select_query =
      "SELECT id, user_id, title, topic, pages, style, include_images, include_charts, include_notes, model_key, model_name, template_id, template_name, status, created_at, updated_at "
      "FROM ppt_requests WHERE id = " +
      std::to_string(request_id) + " LIMIT 1";

  if (mysql_query(conn, select_query.c_str()) != 0) {
    error_message = mysql_error(conn);
    return false;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    error_message = "无法查询PPT请求";
    return false;
  }
  MYSQL_ROW row = mysql_fetch_row(result);
  if (!row) {
    mysql_free_result(result);
    error_message = "请求不存在";
    return false;
  }
  out_request = RowToRequest(row);
  mysql_free_result(result);
  return true;
}

std::vector<PptRequest> PptService::GetHistory(std::uint64_t user_id, std::string& error_message) const {
  std::vector<PptRequest> requests;
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();

  const std::string query =
      "SELECT id, user_id, title, topic, pages, style, include_images, include_charts, include_notes, model_key, model_name, template_id, template_name, status, created_at, updated_at "
      "FROM ppt_requests WHERE user_id = " +
      std::to_string(user_id) + " ORDER BY created_at DESC";

  if (mysql_query(conn, query.c_str()) != 0) {
    error_message = mysql_error(conn);
    return requests;
  }

  MYSQL_RES* result = mysql_store_result(conn);
  if (!result) {
    return requests;
  }

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(result)) != nullptr) {
    requests.push_back(RowToRequest(row));
  }
  mysql_free_result(result);
  return requests;
}

bool PptService::DeleteRequest(std::uint64_t user_id, std::uint64_t request_id, std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();

  std::ostringstream query;
  query << "DELETE FROM ppt_requests WHERE id = " << request_id
        << " AND user_id = " << user_id << " LIMIT 1";

  if (mysql_query(conn, query.str().c_str()) != 0) {
    error_message = mysql_error(conn);
    Logger::Error("删除PPT记录失败: " + error_message);
    return false;
  }

  if (mysql_affected_rows(conn) == 0) {
    error_message = "记录不存在或无权删除";
    return false;
  }

  return true;
}
