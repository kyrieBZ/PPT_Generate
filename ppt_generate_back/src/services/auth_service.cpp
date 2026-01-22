#include <cstdlib>
#include "services/auth_service.h"

#include <sstream>
#include <iomanip>

#include <openssl/rand.h>

#include "logger.h"
#include "utils/crypto.h"

namespace {
std::string EscapeString(MYSQL* connection, const std::string& value) {
  std::string escaped;
  escaped.resize(value.size() * 2 + 1);
  const auto length = mysql_real_escape_string(connection, escaped.data(), value.c_str(), value.length());
  escaped.resize(length);
  return escaped;
}

std::optional<User> ExtractUser(MYSQL_RES* result) {
  MYSQL_ROW row = mysql_fetch_row(result);
  if (!row) {
    return std::nullopt;
  }
  User user;
  user.id = row[0] ? std::strtoull(row[0], nullptr, 10) : 0;
  user.username = row[1] ? row[1] : "";
  user.email = row[2] ? row[2] : "";
  user.password_hash = row[3] ? row[3] : "";
  user.salt = row[4] ? row[4] : "";
  user.created_at = row[5] ? row[5] : "";
  user.updated_at = row[6] ? row[6] : "";
  if (row[7]) {
    user.last_login = std::string(row[7]);
  }
  return user;
}

constexpr int kResetCodeTtlMinutes = 5;

std::string GenerateResetCode() {
  unsigned int value = 0;
  if (RAND_bytes(reinterpret_cast<unsigned char*>(&value), sizeof(value)) != 1) {
    value = static_cast<unsigned int>(std::rand());
  }
  value = value % 1000000;
  std::ostringstream stream;
  stream << std::setw(6) << std::setfill('0') << value;
  return stream.str();
}
}

AuthService::AuthService(std::shared_ptr<MySQLConnectionPool> pool,
                         AuthConfig auth_config,
                         std::shared_ptr<EmailService> email_service)
    : pool_(std::move(pool)),
      auth_config_(auth_config),
      email_service_(std::move(email_service)) {}

bool AuthService::RegisterUser(const std::string& username,
                               const std::string& email,
                               const std::string& password,
                               User& out_user,
                               std::string& out_token,
                               std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();

  if (UsernameExists(conn, username)) {
    error_message = "用户名已存在";
    return false;
  }

  if (EmailExists(conn, email)) {
    error_message = "邮箱已存在";
    return false;
  }

  const auto salt = crypto_utils::GenerateSalt();
  const auto password_hash = crypto_utils::HashPassword(password, salt);

  if (!InsertUser(conn, username, email, password_hash, salt, out_user, error_message)) {
    return false;
  }

  out_token = crypto_utils::GenerateToken();
  if (!StoreToken(conn, out_user.id, out_token, error_message)) {
    return false;
  }

  return true;
}

bool AuthService::Login(const std::string& identifier,
                        const std::string& password,
                        User& out_user,
                        std::string& out_token,
                        std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();

  auto user = FindUserByIdentifier(conn, identifier);
  if (!user) {
    error_message = "用户不存在";
    return false;
  }

  const auto hashed = crypto_utils::HashPassword(password, user->salt);
  if (hashed != user->password_hash) {
    error_message = "密码错误";
    return false;
  }

  const std::string update_query = "UPDATE users SET last_login = NOW() WHERE id = " + std::to_string(user->id) + " LIMIT 1";
  if (mysql_query(conn, update_query.c_str()) != 0) {
    Logger::Warn("无法更新last_login: " + std::string(mysql_error(conn)));
  }

  out_token = crypto_utils::GenerateToken();
  if (!StoreToken(conn, user->id, out_token, error_message)) {
    return false;
  }

  out_user = *user;
  return true;
}

bool AuthService::Logout(const std::string& token, std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  return RemoveToken(conn, token, error_message);
}

std::optional<User> AuthService::GetUserFromToken(const std::string& token, std::string& error_message) const {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  auto user = FindUserByToken(conn, token);
  if (!user) {
    error_message = "Token无效或已过期";
  }
  return user;
}

bool AuthService::RequestPasswordReset(const std::string& email, std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error_message = "无法获取数据库连接";
    return false;
  }

  auto user = FindUserByEmail(conn, email);
  if (!user) {
    return true;  // 避免暴露用户是否存在
  }

  const auto code = GenerateResetCode();
  const auto code_hash = crypto_utils::Sha256(std::to_string(user->id) + ":" + code);

  if (!StoreResetCode(conn, user->id, code_hash, error_message)) {
    return false;
  }

  if (!email_service_ || !email_service_->IsEnabled()) {
    error_message = "邮件服务未配置";
    return false;
  }

  std::ostringstream subject;
  subject << "PPT账号密码重置验证码";
  std::ostringstream content;
  content << "您好，您的验证码为：" << code << "，有效期" << kResetCodeTtlMinutes << "分钟。";

  if (!email_service_->Send(user->email, subject.str(), content.str(), error_message)) {
    return false;
  }

  return true;
}

bool AuthService::ResetPassword(const std::string& email,
                                const std::string& code,
                                const std::string& new_password,
                                std::string& error_message) {
  auto connection = pool_->GetConnection();
  MYSQL* conn = connection.Get();
  if (!conn) {
    error_message = "无法获取数据库连接";
    return false;
  }

  auto user = FindUserByEmail(conn, email);
  if (!user) {
    error_message = "验证码无效或已过期";
    return false;
  }

  const auto code_hash = crypto_utils::Sha256(std::to_string(user->id) + ":" + code);
  auto code_id = FindValidResetCodeId(conn, user->id, code_hash);
  if (!code_id) {
    error_message = "验证码无效或已过期";
    return false;
  }

  const auto salt = crypto_utils::GenerateSalt();
  const auto password_hash = crypto_utils::HashPassword(new_password, salt);
  const auto password_hash_escaped = EscapeString(conn, password_hash);
  const auto salt_escaped = EscapeString(conn, salt);

  std::ostringstream update_query;
  update_query << "UPDATE users SET password_hash='" << password_hash_escaped
               << "', salt='" << salt_escaped << "', updated_at=NOW() WHERE id="
               << user->id << " LIMIT 1";
  if (mysql_query(conn, update_query.str().c_str()) != 0) {
    error_message = mysql_error(conn);
    return false;
  }

  if (!MarkResetCodeUsed(conn, *code_id, error_message)) {
    return false;
  }

  return true;
}

std::optional<User> AuthService::FindUserByIdentifier(MYSQL* connection, const std::string& identifier) const {
  const auto escaped = EscapeString(connection, identifier);
  const std::string query =
      "SELECT id, username, email, password_hash, salt, created_at, updated_at, last_login "
      "FROM users WHERE username='" +
      escaped + "' OR email='" + escaped + "' LIMIT 1";

  if (mysql_query(connection, query.c_str()) != 0) {
    Logger::Error("查询用户失败: " + std::string(mysql_error(connection)));
    return std::nullopt;
  }

  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    return std::nullopt;
  }

  auto user = ExtractUser(result);
  mysql_free_result(result);
  return user;
}

std::optional<User> AuthService::FindUserByEmail(MYSQL* connection, const std::string& email) const {
  const auto escaped = EscapeString(connection, email);
  const std::string query =
      "SELECT id, username, email, password_hash, salt, created_at, updated_at, last_login "
      "FROM users WHERE email='" +
      escaped + "' LIMIT 1";

  if (mysql_query(connection, query.c_str()) != 0) {
    Logger::Error("查询邮箱用户失败: " + std::string(mysql_error(connection)));
    return std::nullopt;
  }

  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    return std::nullopt;
  }

  auto user = ExtractUser(result);
  mysql_free_result(result);
  return user;
}

bool AuthService::UsernameExists(MYSQL* connection, const std::string& username) const {
  const auto escaped = EscapeString(connection, username);
  const std::string query = "SELECT id FROM users WHERE username='" + escaped + "' LIMIT 1";
  if (mysql_query(connection, query.c_str()) != 0) {
    Logger::Error("检查用户名失败: " + std::string(mysql_error(connection)));
    return true;
  }
  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    return true;
  }
  bool exists = mysql_num_rows(result) > 0;
  mysql_free_result(result);
  return exists;
}

bool AuthService::EmailExists(MYSQL* connection, const std::string& email) const {
  const auto escaped = EscapeString(connection, email);
  const std::string query = "SELECT id FROM users WHERE email='" + escaped + "' LIMIT 1";
  if (mysql_query(connection, query.c_str()) != 0) {
    Logger::Error("检查邮箱失败: " + std::string(mysql_error(connection)));
    return true;
  }
  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    return true;
  }
  bool exists = mysql_num_rows(result) > 0;
  mysql_free_result(result);
  return exists;
}

bool AuthService::InsertUser(MYSQL* connection,
                             const std::string& username,
                             const std::string& email,
                             const std::string& password_hash,
                             const std::string& salt,
                             User& created_user,
                             std::string& error_message) const {
  const auto username_escaped = EscapeString(connection, username);
  const auto email_escaped = EscapeString(connection, email);
  const auto hash_escaped = EscapeString(connection, password_hash);
  const auto salt_escaped = EscapeString(connection, salt);

  const std::string insert_query =
      "INSERT INTO users (username, email, password_hash, salt) VALUES ('" + username_escaped + "', '" +
      email_escaped + "', '" + hash_escaped + "', '" + salt_escaped + "')";

  if (mysql_query(connection, insert_query.c_str()) != 0) {
    error_message = mysql_error(connection);
    Logger::Error("插入用户失败: " + error_message);
    return false;
  }

  const auto user_id = mysql_insert_id(connection);
  const std::string select_query =
      "SELECT id, username, email, password_hash, salt, created_at, updated_at, last_login FROM users WHERE id = " +
      std::to_string(user_id) + " LIMIT 1";

  if (mysql_query(connection, select_query.c_str()) != 0) {
    error_message = mysql_error(connection);
    return false;
  }

  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    error_message = "无法读取新用户";
    return false;
  }

  auto user = ExtractUser(result);
  mysql_free_result(result);
  if (!user) {
    error_message = "无法解析新用户";
    return false;
  }

  created_user = *user;
  return true;
}

bool AuthService::StoreToken(MYSQL* connection, std::uint64_t user_id, const std::string& token, std::string& error_message) const {
  std::ostringstream query;
  query << "INSERT INTO auth_tokens (user_id, token, expires_at) VALUES ("
        << user_id << ", '" << EscapeString(connection, token) << "', DATE_ADD(NOW(), INTERVAL "
        << auth_config_.token_ttl_minutes << " MINUTE))";

  if (mysql_query(connection, query.str().c_str()) != 0) {
    error_message = mysql_error(connection);
    Logger::Error("保存Token失败: " + error_message);
    return false;
  }

  const std::string cleanup_query = "DELETE FROM auth_tokens WHERE expires_at < NOW()";
  mysql_query(connection, cleanup_query.c_str());
  return true;
}

bool AuthService::StoreResetCode(MYSQL* connection,
                                 std::uint64_t user_id,
                                 const std::string& code_hash,
                                 std::string& error_message) const {
  const auto hash_escaped = EscapeString(connection, code_hash);
  const std::string cleanup_query =
      "DELETE FROM password_reset_codes WHERE user_id=" + std::to_string(user_id) + " OR expires_at < NOW()";
  mysql_query(connection, cleanup_query.c_str());

  std::ostringstream query;
  query << "INSERT INTO password_reset_codes (user_id, code_hash, expires_at) VALUES ("
        << user_id << ", '" << hash_escaped << "', DATE_ADD(NOW(), INTERVAL "
        << kResetCodeTtlMinutes << " MINUTE))";

  if (mysql_query(connection, query.str().c_str()) != 0) {
    error_message = mysql_error(connection);
    Logger::Error("保存重置验证码失败: " + error_message);
    return false;
  }
  return true;
}

std::optional<std::uint64_t> AuthService::FindValidResetCodeId(MYSQL* connection,
                                                               std::uint64_t user_id,
                                                               const std::string& code_hash) const {
  const auto hash_escaped = EscapeString(connection, code_hash);
  const std::string query =
      "SELECT id FROM password_reset_codes WHERE user_id=" + std::to_string(user_id) +
      " AND code_hash='" + hash_escaped + "' AND used_at IS NULL AND expires_at > NOW() "
      "ORDER BY id DESC LIMIT 1";

  if (mysql_query(connection, query.c_str()) != 0) {
    Logger::Error("查询重置验证码失败: " + std::string(mysql_error(connection)));
    return std::nullopt;
  }

  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    return std::nullopt;
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  if (!row || !row[0]) {
    mysql_free_result(result);
    return std::nullopt;
  }
  const auto id = std::strtoull(row[0], nullptr, 10);
  mysql_free_result(result);
  return id;
}

bool AuthService::MarkResetCodeUsed(MYSQL* connection, std::uint64_t code_id, std::string& error_message) const {
  const std::string query =
      "UPDATE password_reset_codes SET used_at = NOW() WHERE id=" + std::to_string(code_id) + " LIMIT 1";
  if (mysql_query(connection, query.c_str()) != 0) {
    error_message = mysql_error(connection);
    Logger::Error("更新验证码状态失败: " + error_message);
    return false;
  }
  return true;
}

bool AuthService::RemoveToken(MYSQL* connection, const std::string& token, std::string& error_message) const {
  const auto escaped = EscapeString(connection, token);
  const std::string query = "DELETE FROM auth_tokens WHERE token='" + escaped + "'";
  if (mysql_query(connection, query.c_str()) != 0) {
    error_message = mysql_error(connection);
    return false;
  }
  return true;
}

std::optional<User> AuthService::FindUserByToken(MYSQL* connection, const std::string& token) const {
  const auto escaped = EscapeString(connection, token);
  const std::string query =
      "SELECT u.id, u.username, u.email, u.password_hash, u.salt, u.created_at, u.updated_at, u.last_login "
      "FROM auth_tokens t INNER JOIN users u ON t.user_id = u.id "
      "WHERE t.token='" +
      escaped + "' AND t.expires_at > NOW() LIMIT 1";

  if (mysql_query(connection, query.c_str()) != 0) {
    Logger::Error("根据Token查询用户失败: " + std::string(mysql_error(connection)));
    return std::nullopt;
  }
  MYSQL_RES* result = mysql_store_result(connection);
  if (!result) {
    return std::nullopt;
  }
  auto user = ExtractUser(result);
  mysql_free_result(result);
  return user;
}
