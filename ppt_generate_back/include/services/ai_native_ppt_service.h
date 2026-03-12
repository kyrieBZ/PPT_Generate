#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "models/outline_item.h"

// 前向声明，避免头文件循环依赖
class WanxiangImageClient;

struct AiNativeGenerationConfig {
  std::string node_binary = "node";
  std::string ai_native_builder_script;
  std::string output_dir;
  std::string image_dir;
};

class AiNativePptService {
 public:
  explicit AiNativePptService(std::string qwen_api_key,
                               std::uint32_t timeout_seconds = 120);

  bool IsEnabled() const { return !qwen_api_key_.empty(); }

  /**
   * 完整的 AI 原生生成流程（四阶段）：
   *   Phase 1: GenerateCreativeBrief  — 主题分析 + 设计决策
   *   Phase 2: GenerateDesignSpec     — 每张幻灯片的完整元素规格
   *   Phase 3: FillSlideContents      — 填充具体文字内容
   *   Phase 4: FetchImages            — 调用 Wanxiang 为 image 元素生成真实图片
   *
   * @param include_images  是否包含图片（控制 Phase 2 是否生成 image 元素 + Phase 4 是否执行）
   * @param include_charts  是否包含图表（控制 Phase 2 是否生成 chart 元素）
   * @param wanx_client     万象图片客户端指针（可为 nullptr，为 nullptr 时跳过图片生成）
   */
  bool Generate(const std::string& topic,
                const std::string& style,
                int pages,
                const std::string& ai_style_prompt,
                const std::vector<OutlineItem>& outline,
                const std::string& output_path,
                const AiNativeGenerationConfig& config,
                std::string& error_message,
                bool include_images = false,
                bool include_charts = false,
                WanxiangImageClient* wanx_client = nullptr) const;

 private:
  bool GenerateCreativeBrief(const std::string& topic,
                              const std::string& style,
                              int pages,
                              const std::string& ai_style_prompt,
                              std::string& out_brief_json,
                              std::string& error) const;

  bool GenerateDesignSpec(const std::string& brief_json,
                           const std::string& outline_json,
                           const std::string& topic,
                           bool include_images,
                           bool include_charts,
                           std::string& out_spec_json,
                           std::string& error) const;

  bool FillSlideContents(const std::string& topic,
                          const std::string& brief_json,
                          std::string& inout_spec_json,
                          std::string& error) const;

  /**
   * Phase 4：遍历 spec 中所有 image 类型元素，
   * 用 image_prompt 调用 Wanxiang 生成真实图片并将本地路径写回 elements[].path。
   */
  bool FetchImages(std::string& inout_spec_json,
                   const AiNativeGenerationConfig& config,
                   std::uint64_t request_id,
                   WanxiangImageClient* wanx_client,
                   std::string& error) const;

  bool RunAiNativeBuilder(const std::string& spec_json,
                           const std::string& output_path,
                           const AiNativeGenerationConfig& config,
                           std::string& error) const;

  std::string CallQwen(const std::string& system_prompt,
                        const std::string& user_prompt,
                        std::string& error) const;

  std::string ExtractJson(const std::string& text) const;

  std::string qwen_api_key_;
  std::uint32_t timeout_seconds_;
};
