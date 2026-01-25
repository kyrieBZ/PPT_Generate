#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "app_config.h"
#include "database/mysql_connection_pool.h"
#include "models/user.h"
#include "services/email_service.h"

class AuthService {
 public:
  AuthService(std::shared_ptr<MySQLConnectionPool> pool,
              AuthConfig auth_config,
              AdminConfig admin_config,
              std::shared_ptr<EmailService> email_service);

  bool RegisterUser(const std::string& username,
                    const std::string& email,
                    const std::string& password,
                    User& out_user,
                    std::string& out_token,
                    std::string& error_message);

  bool Login(const std::string& identifier,
             const std::string& password,
             User& out_user,
             std::string& out_token,
             std::string& error_message);

  bool Logout(const std::string& token, std::string& error_message);

  std::optional<User> GetUserFromToken(const std::string& token, std::string& error_message) const;

  bool RequestPasswordReset(const std::string& email, std::string& error_message);
  bool ResetPassword(const std::string& email,
                     const std::string& code,
                     const std::string& new_password,
                     std::string& error_message);

  bool IsAdmin(const User& user) const;

  std::vector<User> ListUsers(const std::string& query, std::string& error_message) const;
  bool UpdateUserStatus(std::uint64_t user_id, bool disabled, std::string& error_message) const;

 private:
  void ApplyAdmin(User& user) const;
  std::optional<User> FindUserByIdentifier(MYSQL* connection, const std::string& identifier) const;
  bool UsernameExists(MYSQL* connection, const std::string& username) const;
  bool EmailExists(MYSQL* connection, const std::string& email) const;
  bool InsertUser(MYSQL* connection,
                  const std::string& username,
                  const std::string& email,
                  const std::string& password_hash,
                  const std::string& salt,
                  User& created_user,
                  std::string& error_message) const;
  bool StoreToken(MYSQL* connection, std::uint64_t user_id, const std::string& token, std::string& error_message) const;
  bool RemoveToken(MYSQL* connection, const std::string& token, std::string& error_message) const;
  std::optional<User> FindUserByToken(MYSQL* connection, const std::string& token) const;
  std::optional<User> FindUserByEmail(MYSQL* connection, const std::string& email) const;
  bool StoreResetCode(MYSQL* connection,
                      std::uint64_t user_id,
                      const std::string& code_hash,
                      std::string& error_message) const;
  std::optional<std::uint64_t> FindValidResetCodeId(MYSQL* connection,
                                                    std::uint64_t user_id,
                                                    const std::string& code_hash) const;
  bool MarkResetCodeUsed(MYSQL* connection, std::uint64_t code_id, std::string& error_message) const;

  std::shared_ptr<MySQLConnectionPool> pool_;
  AuthConfig auth_config_;
  std::unordered_set<std::string> admin_usernames_;
  std::unordered_set<std::string> admin_emails_;
  std::shared_ptr<EmailService> email_service_;
};
