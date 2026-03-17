#pragma once
#include <cstdint>
#include <string>

struct Announcement {
  std::uint64_t id = 0;
  std::string title;
  std::string content;
  bool is_pinned = false;
  std::uint64_t starts_at = 0;   // unix timestamp
  std::uint64_t expires_at = 0;  // 0 = never expires
  std::uint64_t created_by = 0;
  std::uint64_t created_at = 0;
  std::uint64_t updated_at = 0;
};
