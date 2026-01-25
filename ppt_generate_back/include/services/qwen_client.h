#pragma once

#include <optional>
#include <string>
#include <vector>

#include "models/outline_item.h"
#include "models/slide_content.h"

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

  bool GenerateOutline(const std::string& topic,
                       int slide_count,
                       const std::string& template_hint,
                       std::vector<OutlineItem>& out_outline,
                       std::string& error_message) const;

  bool GenerateSlidesFromOutline(const std::string& topic,
                                 const std::vector<OutlineItem>& outline,
                                 bool include_images,
                                 std::vector<SlideContent>& out_slides,
                                 std::string& error_message) const;

 private:
  std::string api_key_;
};
