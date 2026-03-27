#pragma once

#include <memory>
#include <string>
#include <vector>

#include "services/qwen_client.h"

/**
 * TemplateRecommendService — F07 智能模板匹配推荐
 *
 * 功能：
 *   1. IndexTemplates()：将模板库中所有模板的描述+标签向量化后存入 Qdrant
 *      ppt_templates collection（服务启动时或管理员触发时调用一次）
 *   2. Recommend()：给定用户输入的 topic，先用 Qwen 分析主题属性
 *      {industry, scene, audience, tone}，再向量检索 Top-N，
 *      最后用 Qwen 为每个推荐结果生成一句 match_reason
 *
 * 与 KnowledgeRagService 相同，通过 qdrant_base_url_ 直接操作 Qdrant REST API，
 * 不复用绑定到固定 collection 的 QdrantClient。
 */
class TemplateRecommendService {
 public:
  struct TemplateInfo {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> tags;
    std::string primary_color;
    std::string accent_color;
    std::string preview_image;
    std::string provider;
  };

  struct RecommendResult {
    std::string template_id;
    std::string name;
    std::string description;
    std::string match_reason;
    double score = 0.0;
    std::string primary_color;
    std::string accent_color;
    std::string preview_image;
    std::string provider;
  };

  /**
   * @param qwen_client     已初始化的 QwenClient（用于 Embedding + 分析推理）
   * @param qdrant_base_url Qdrant REST 基础 URL，如 "http://localhost:6333"
   * @param vector_dim      向量维度，需与 Qdrant collection 配置一致（默认 1024）
   */
  TemplateRecommendService(std::shared_ptr<QwenClient> qwen_client,
                           std::string qdrant_base_url,
                           int vector_dim = 1024);

  bool IsAvailable() const;

  /**
   * 确保 ppt_templates collection 存在。
   */
  bool EnsureCollection(std::string& error);

  /**
   * 将模板列表向量化并批量写入 Qdrant ppt_templates collection。
   * 幂等：已存在的点会被覆盖（upsert）。
   * @return  成功写入的模板数；-1 表示失败
   */
  int IndexTemplates(const std::vector<TemplateInfo>& templates, std::string& error);

  /**
   * 给定主题，返回最匹配的模板推荐列表。
   * 流程：Embedding(topic) → Qdrant 向量检索 → Qwen 生成 match_reason
   *
   * @param topic   用户输入的主题描述
   * @param top_k   返回推荐数量（默认 5）
   * @return        按相关性排序的推荐结果；服务不可用时返回空列表
   */
  std::vector<RecommendResult> Recommend(const std::string& topic, int top_k = 5) const;

  /**
   * 统计 ppt_templates collection 中的已索引模板数量。
   */
  int CountIndexed(std::string& error) const;

 private:
  /** 为一个模板构建用于 Embedding 的描述文本 */
  static std::string BuildIndexText(const TemplateInfo& tmpl);

  /** 向 Qdrant ppt_templates collection 发送 REST 请求 */
  std::string DoQdrantRequest(const std::string& method,
                              const std::string& path,
                              const std::string& body,
                              int& http_code) const;

  /**
   * 调用 Qwen 为推荐结果生成 match_reason。
   * 仅尝试一次，失败时使用 fallback reason。
   */
  void EnrichWithReasons(const std::string& topic,
                         std::vector<RecommendResult>& results) const;

  std::shared_ptr<QwenClient> qwen_client_;
  std::string qdrant_base_url_;
  int vector_dim_;

  static constexpr const char* kCollectionName = "ppt_templates";
};
