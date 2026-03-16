#pragma once

#include <cstdint>
#include <functional>
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

// 进度回调：(progress 0-100, stage 阶段名, step 步骤描述)
using ProgressCallback = std::function<void(int, const std::string&, const std::string&)>;

class AiNativePptService {
 public:
  explicit AiNativePptService(std::string qwen_api_key,
                               std::uint32_t timeout_seconds = 120);

  bool IsEnabled() const { return !qwen_api_key_.empty(); }

  /**
   * 完整的 AI 原生生成流程（四阶段）：
   *   Phase 1: GenerateCreativeBrief  — 主题分析 + 设计决策
   *   Phase 2: GenerateDesignSpec     — 每张幻灯片的完整元素规格（分 batch）
   *   Phase 3: FillSlideContents      — 逐张填充具体文字内容
   *   Phase 4: FetchImages            — 逐张调用 Wanxiang 生成图片
   *
   * @param on_progress  进度回调（可为 nullptr），每个子步骤完成时调用
   */
  /** material_context: 当从文献生成时传入（参考材料关键信息+约束），非空时正文与图表数据必须严格来自该内容 */
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
                WanxiangImageClient* wanx_client = nullptr,
                ProgressCallback on_progress = nullptr,
                const std::string& material_context = "") const;

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
                           std::string& error,
                           int total_slides,
                           const ProgressCallback& on_progress) const;

  bool FillSlideContents(const std::string& topic,
                          const std::string& brief_json,
                          std::string& inout_spec_json,
                          std::string& error,
                          int total_slides,
                          const ProgressCallback& on_progress) const;

  bool FetchImages(std::string& inout_spec_json,
                   const AiNativeGenerationConfig& config,
                   std::uint64_t request_id,
                   WanxiangImageClient* wanx_client,
                   std::string& error,
                   int total_images,
                   const ProgressCallback& on_progress) const;

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
