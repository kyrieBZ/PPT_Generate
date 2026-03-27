#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "services/qdrant_client.h"
#include "services/qwen_client.h"

/**
 * KnowledgeRagService — 基于 RAG 的领域知识增强生成服务
 *
 * 功能：
 *   1. 将已提取完成的素材文本分块（Chunking）并向量化后存入专用 Qdrant collection
 *      （collection 名称：user_knowledge）
 *   2. PPT 生成时，按当前幻灯片主题检索最相关的知识块（Top-K）
 *   3. 将检索结果格式化为 Prompt 片段，供 QwenClient 注入
 *
 * 向量存储结构（Qdrant payload）：
 *   {
 *     "chunk_id":    "material_id__chunk_0",
 *     "material_id": "xxx",
 *     "user_id":     123,
 *     "chunk_index": 0,
 *     "chunk_text":  "...",
 *     "filename":    "report.pdf"
 *   }
 *
 * Collection 名称固定为 "user_knowledge"，与 AiSearchService 的 "ppt_index" collection 分离。
 *
 * 注意：由于 QdrantClient 的 collection 在构造时固定，此服务通过 qdrant_base_url_ 直接
 * 操作 user_knowledge collection，不复用 QdrantClient 的内部方法。
 */
class KnowledgeRagService {
 public:
  struct KnowledgeChunk {
    std::string chunk_id;
    std::string material_id;
    std::uint64_t user_id = 0;
    int chunk_index = 0;
    std::string chunk_text;
    std::string filename;
    double score = 0.0;
  };

  /**
   * @param qdrant_client   已初始化的 QdrantClient（用于探测 Qdrant 可用性）
   * @param qwen_client     已初始化的 QwenClient（用于 Embedding）
   * @param qdrant_base_url Qdrant REST 基础 URL，如 "http://localhost:6333"
   * @param chunk_size      每个文本块的目标字符数（默认 500）
   * @param chunk_overlap   相邻块之间的重叠字符数（默认 50）
   * @param vector_dim      向量维度，需与 Qdrant collection 配置一致（默认 1024）
   */
  KnowledgeRagService(std::shared_ptr<QdrantClient> qdrant_client,
                      std::shared_ptr<QwenClient> qwen_client,
                      std::string qdrant_base_url,
                      int chunk_size   = 500,
                      int chunk_overlap = 50,
                      int vector_dim   = 1024);

  bool IsAvailable() const;

  /**
   * 确保 user_knowledge collection 存在。
   * 在服务启动时调用一次。
   */
  bool EnsureCollection(std::string& error);

  /**
   * 将素材的提取文本分块 → 向量化 → 存入 Qdrant。
   *
   * @param material_id   素材 ID（UUID）
   * @param user_id       素材所属用户 ID
   * @param extract_text  提取文本（来自 materials.extract_result 中的 key_points/summary 字段拼接）
   * @param filename      素材文件名（用于检索结果展示）
   * @param error         失败时填充错误信息
   * @return              成功写入的块数；失败返回 -1
   */
  int IndexMaterial(const std::string& material_id,
                    std::uint64_t user_id,
                    const std::string& extract_text,
                    const std::string& filename,
                    std::string& error);

  /**
   * 删除指定素材的所有向量块（素材删除时调用）。
   */
  bool RemoveMaterial(const std::string& material_id, std::string& error);

  /**
   * 语义检索：给定查询文本和用户 ID，返回最相关的 Top-K 知识块。
   *
   * @param query      检索文本（幻灯片标题或主题描述）
   * @param user_id    限定到该用户的素材
   * @param material_ids  若非空，只检索这些素材（用户在生成时勾选了哪些素材）
   * @param top_k      返回块数（默认 3）
   * @return           按相关性排序的知识块列表
   */
  std::vector<KnowledgeChunk> Retrieve(const std::string& query,
                                       std::uint64_t user_id,
                                       const std::vector<std::string>& material_ids,
                                       int top_k = 3) const;

  /**
   * 将检索到的知识块格式化为 Prompt 注入片段。
   * 格式：
   *   【知识库参考资料】
   *   来源：xxx.pdf
   *   内容：...
   *   ---
   *   来源：yyy.docx
   *   内容：...
   */
  static std::string FormatChunksAsContext(const std::vector<KnowledgeChunk>& chunks);

  /**
   * 统计某用户在 user_knowledge collection 中的索引块数。
   */
  int CountUserChunks(std::uint64_t user_id, std::string& error) const;

 private:
  /**
   * 将文本切分为若干重叠块。
   * 按句号、换行等自然边界分割，尽量不截断句子。
   */
  std::vector<std::string> SplitIntoChunks(const std::string& text) const;

  /**
   * 从 extract_result JSON 中提取适合 RAG 索引的纯文本。
   * 优先拼接：summary + key_points + data_mentions
   */
  static std::string ExtractIndexableText(const std::string& extract_result_json);

  /**
   * 直接向 Qdrant REST API 发送请求（操作 user_knowledge collection）。
   */
  std::string DoQdrantRequest(const std::string& method,
                              const std::string& path,
                              const std::string& body,
                              int& http_code) const;  // const: uses libcurl (stateless)

  std::shared_ptr<QdrantClient> qdrant_client_;
  std::shared_ptr<QwenClient> qwen_client_;
  std::string qdrant_base_url_;
  int chunk_size_;
  int chunk_overlap_;
  int vector_dim_;

  static constexpr const char* kCollectionName = "user_knowledge";

 public:
  /**
   * 从 extract_result JSON 中提取可索引文本（供 MaterialService 调用）。
   */
  static std::string BuildIndexableText(const std::string& extract_result_json) {
    return ExtractIndexableText(extract_result_json);
  }
};
