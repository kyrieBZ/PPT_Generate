#pragma once

#include <memory>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "services/template_service.h"
#include "services/template_manager_service.h"
#include "services/template_fastdfs_service.h"
#include "services/template_recommend_service.h"

class TemplateController {
 public:
  /**
   * @param tmpl_mgr_service       可为 nullptr（兼容旧逻辑）；
   *   非 nullptr 时 List 接口将只返回已上架且在有效期内的模板。
   * @param tmpl_fastdfs_service   可为 nullptr；
   *   非 nullptr 且 FastDFS 已启用时，Download/Preview 优先重定向到 FastDFS URL。
   * @param tmpl_recommend_service 可为 nullptr；
   *   非 nullptr 时启用 POST /api/templates/recommend 接口。
   */
  TemplateController(std::shared_ptr<TemplateService>          service,
                     std::shared_ptr<TemplateManagerService>   tmpl_mgr_service = nullptr,
                     std::shared_ptr<TemplateFastDfsService>   tmpl_fastdfs_service = nullptr,
                     std::shared_ptr<TemplateRecommendService> tmpl_recommend_service = nullptr);

  HttpResponse List(const HttpRequest& request);
  HttpResponse Download(const HttpRequest& request);
  HttpResponse Preview(const HttpRequest& request);

  /**
   * POST /api/templates/recommend
   * Body: { "topic": "主题描述", "top_k": 5 }
   * 返回: { "available": bool, "recommendations": [...] }
   */
  HttpResponse Recommend(const HttpRequest& request);

  /**
   * POST /api/admin/templates/reindex (管理员接口)
   * 将当前模板库全量写入 Qdrant ppt_templates collection。
   */
  HttpResponse ReindexTemplates(const HttpRequest& request);

 private:
  static nlohmann::json ToJson(const RemoteTemplate& item);

  std::shared_ptr<TemplateService>          service_;
  std::shared_ptr<TemplateManagerService>   tmpl_mgr_service_;
  std::shared_ptr<TemplateFastDfsService>   tmpl_fastdfs_service_;
  std::shared_ptr<TemplateRecommendService> tmpl_recommend_service_;
};
