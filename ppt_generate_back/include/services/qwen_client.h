#pragma once

#include <optional>
#include <string>
#include <vector>

#include "models/outline_item.h"
#include "models/slide_content.h"

class QwenClient {
 public:
  /** timeout_seconds: HTTP 请求超时，0 表示使用默认 60 秒 */
  explicit QwenClient(std::string api_key, std::uint32_t timeout_seconds = 60);

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
                                 std::string& error_message,
                                 bool include_charts = false,
                                 bool include_notes = false) const;

  bool GenerateLayoutGuide(const std::string& template_summary_json,
                           int slide_count,
                           const std::string& template_hint,
                           std::string& out_layout_json,
                           std::string& error_message) const;

  bool GenerateSlidesFromOutlineWithLayout(const std::string& topic,
                                           const std::vector<OutlineItem>& outline,
                                           bool include_images,
                                           const std::string& layout_guide_json,
                                           std::vector<SlideContent>& out_slides,
                                           std::string& error_message,
                                           bool include_charts = false,
                                           bool include_notes = false) const;

  bool GenerateSlidesWithLayout(const std::string& topic,
                                int slide_count,
                                const std::string& template_hint,
                                bool include_images,
                                const std::string& layout_guide_json,
                                std::vector<SlideContent>& out_slides,
                                std::string& error_message,
                                bool include_charts = false,
                                bool include_notes = false) const;

  struct KeywordFreq {
    std::string keyword;
    int count = 0;
  };

  // Extract high-frequency keywords from a list of PPT topics using LLM
  // topics: raw topic strings from ppt_requests
  // out_keywords: deduplicated keywords sorted by descending frequency
  bool ExtractKeywords(const std::vector<std::string>& topics,
                       std::vector<KeywordFreq>& out_keywords,
                       std::string& error_message) const;

  // Generate embedding vector for the given text using DashScope text-embedding API
  // model: e.g. "text-embedding-v3"
  // Returns empty vector on failure
  std::vector<float> GetEmbedding(const std::string& text,
                                  const std::string& model = "text-embedding-v3") const;

  // Rerank candidates and generate a one-sentence match reason for each
  // Returns reranked list of (ppt_id, reason) pairs, at most top_k items
  struct RerankResult {
    std::uint64_t ppt_id = 0;
    std::string reason;
    double score = 0.0;
  };
  std::vector<RerankResult> RerankWithReason(
      const std::string& query,
      const std::vector<std::uint64_t>& candidate_ids,
      const std::vector<std::string>& candidate_summaries,
      int top_k,
      const std::string& model = "qwen-plus") const;

  // Analyze images using Qwen-VL multimodal API and extract structured content
  // images_base64: list of base64-encoded image data strings (with or without data URI prefix)
  // out_description: extracted structured description for PPT generation
  // Returns true on success
  bool AnalyzeImages(const std::vector<std::string>& images_base64,
                     const std::string& user_hint,
                     std::string& out_description,
                     std::string& error_message) const;

 private:
  std::string api_key_;
  std::uint32_t timeout_seconds_;
};
