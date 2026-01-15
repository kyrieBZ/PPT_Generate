#include "utils/string_utils.h"

#include <cctype>
#include <sstream>

namespace string_utils {

std::string Trim(const std::string& input) {
  const auto begin = input.find_first_not_of(" \t\n\r");
  if (begin == std::string::npos) {
    return "";
  }
  const auto end = input.find_last_not_of(" \t\n\r");
  return input.substr(begin, end - begin + 1);
}

std::string ToLower(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string UrlDecode(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const std::string hex = value.substr(i + 1, 2);
      char* end = nullptr;
      const long code = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        result.push_back(static_cast<char>(code));
        i += 2;
        continue;
      }
    } else if (value[i] == '+') {
      result.push_back(' ');
      continue;
    }
    result.push_back(value[i]);
  }
  return result;
}

std::unordered_map<std::string, std::string> ParseQuery(const std::string& query_string) {
  std::unordered_map<std::string, std::string> params;
  std::size_t start = 0;
  while (start < query_string.size()) {
    auto end = query_string.find('&', start);
    if (end == std::string::npos) {
      end = query_string.size();
    }
    auto token = query_string.substr(start, end - start);
    if (!token.empty()) {
      auto eq = token.find('=');
      if (eq != std::string::npos) {
        auto key = UrlDecode(token.substr(0, eq));
        auto value = UrlDecode(token.substr(eq + 1));
        params[key] = value;
      } else {
        params[UrlDecode(token)] = "";
      }
    }
    start = end + 1;
  }
  return params;
}

}  // namespace string_utils
