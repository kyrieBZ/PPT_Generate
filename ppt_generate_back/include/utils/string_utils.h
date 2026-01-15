#pragma once

#include <string>
#include <unordered_map>

namespace string_utils {

std::string Trim(const std::string& input);
std::string ToLower(std::string value);
std::string UrlDecode(const std::string& value);
std::unordered_map<std::string, std::string> ParseQuery(const std::string& query_string);

}
