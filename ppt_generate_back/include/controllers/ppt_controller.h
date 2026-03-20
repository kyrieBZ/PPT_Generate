#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

#include "app_config.h"
#include "database/mysql_connection_pool.h"
#include "database/redis_client.h"
#include "http/http_types.h"
#include "services/ai_search_service.h"
#include "services/auth_service.h"
#include "services/material_service.h"
#include "services/model_service.h"
#include "services/ppt_service.h"
#include "services/qwen_client.h"
#include "services/s3_client.h"
#include "services/template_service.h"
#include "services/wanxiang_image_client.h"
#include "utils/thread_pool.h"

class PptController {
 public:
  PptController(std::shared_ptr<AuthService> auth_service,
                std::shared_ptr<PptService> ppt_service,
                std::shared_ptr<ModelService> model_service,
                std::shared_ptr<TemplateService> template_service,
                GenerationConfig generation_config,
                std::shared_ptr<QwenClient> qwen_client,
                std::shared_ptr<S3Client> s3_client,
                std::shared_ptr<WanxiangImageClient> wanx_client,
                std::shared_ptr<ThreadPool> thread_pool,
                std::shared_ptr<MaterialService> material_service = nullptr,
                std::shared_ptr<RedisClient> redis = nullptr,
                int redis_ttl_ppt_status = 7200,
                int redis_ttl_ppt_history = 300,
                std::shared_ptr<MySQLConnectionPool> pool = nullptr,
                std::shared_ptr<AiSearchService> ai_search_service = nullptr);

  HttpResponse Generate(const HttpRequest& request);
  /** 轮询单条请求状态，用于异步生成后查询 completed/failed */
  HttpResponse GetRequestStatus(const HttpRequest& request);
  HttpResponse History(const HttpRequest& request);
  HttpResponse AdminHistory(const HttpRequest& request);
  HttpResponse AdminMetrics(const HttpRequest& request);
  HttpResponse AdminInsights(const HttpRequest& request);
  /** GET /api/admin/export/ppt_history — 导出生成记录 CSV（支持 ?q= 筛选）*/
  HttpResponse AdminExportPptHistory(const HttpRequest& request);
  HttpResponse Delete(const HttpRequest& request);
  /** POST /api/ppt/batch_delete  — body: {"ids":[...]}，批量删除（最多 50 条） */
  HttpResponse BatchDelete(const HttpRequest& request);
  HttpResponse Download(const HttpRequest& request);
  /** POST /api/ppt/batch_download  — body: {"ids":[...]}，生成 ZIP 并返回 download_url */
  HttpResponse BatchDownload(const HttpRequest& request);
  /** GET /api/ppt/batch_zip?token=xxx  — 流式返回已生成的批量 ZIP 文件 */
  HttpResponse BatchDownloadFile(const HttpRequest& request);
  HttpResponse Preview(const HttpRequest& request);
  HttpResponse Outline(const HttpRequest& request);

  // 在线编辑：PPT 结构化 JSON 读写与再生成
  HttpResponse GetStructure(const HttpRequest& request);
  HttpResponse UpdateStructure(const HttpRequest& request);
  HttpResponse RegenerateFromStructure(const HttpRequest& request);

 private:
  std::shared_ptr<User> Authenticate(const HttpRequest& request, std::string& error_message) const;
  std::uint64_t ParseId(const std::string& str) const;

  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<PptService> ppt_service_;
  std::shared_ptr<ModelService> model_service_;
  std::shared_ptr<TemplateService> template_service_;
  GenerationConfig generation_config_;
  std::shared_ptr<QwenClient> qwen_client_;
  std::shared_ptr<S3Client> s3_client_;
  std::shared_ptr<WanxiangImageClient> wanx_client_;
  std::shared_ptr<ThreadPool> thread_pool_;
  std::shared_ptr<MaterialService> material_service_;
  std::shared_ptr<RedisClient>         redis_;
  int redis_ttl_ppt_status_  = 7200;
  int redis_ttl_ppt_history_ = 300;
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<AiSearchService>     ai_search_service_;

  /** 当前正在执行（已提交到线程池）的生成任务数 */
  mutable std::atomic<int> active_jobs_{0};
};
