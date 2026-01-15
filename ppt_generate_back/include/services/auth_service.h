#pragma once

#include <memory>
#include <optional>
#include <string>

#include "app_config.h"
#include "database/mysql_connection_pool.h"
#include "models/user.h"

class AuthService {
 public:
  AuthService(std::shared_ptr<MySQLConnectionPool> pool, AuthConfig auth_config);

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

 private:
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

  std::shared_ptr<MySQLConnectionPool> pool_;
  AuthConfig auth_config_;
};
