#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct User {
  std::uint64_t id = 0;
  std::string username;
  std::string email;
  std::string password_hash;
  std::string salt;
  std::string created_at;
  std::string updated_at;
  std::optional<std::string> last_login;
  bool is_admin = false;
  bool is_disabled = false;
};
