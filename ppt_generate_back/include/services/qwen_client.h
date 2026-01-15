#pragma once

#include <optional>
#include <string>
#include <vector>

struct SlideContent {
  std::string title;
  std::vector<std::string> bullets;
  std::string raw_text;
  std::vector<std::string> image_prompts;
  std::vector<std::string> image_urls;
};

class QwenClient {
 public:
  explicit QwenClient(std::string api_key);

  bool IsEnabled() const { return !api_key_.empty(); }

  bool GenerateSlides(const std::string& topic,
                      int slide_count,
                      const std::string& template_hint,
                      bool include_images,
                      std::vector<SlideContent>& out_slides,
                      std::string& error_message) const;

 private:
  std::string api_key_;
};
