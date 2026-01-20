#pragma once

#include <string>
#include <vector>

struct SlideContent {
  std::string title;
  std::vector<std::string> bullets;
  std::string raw_text;
  std::vector<std::string> image_prompts;
  std::vector<std::string> image_urls;
  std::vector<std::string> suggestions;
  std::string layout_hint;
  std::string notes;
};
