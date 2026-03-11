#pragma once

#include <string>
#include <vector>

struct OutlineItem {
  std::string title;
  std::vector<std::string> key_points;
  std::string summary;
  std::string page_type;  // cover / toc / content / summary
};
