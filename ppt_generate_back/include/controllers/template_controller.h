#pragma once

#include <memory>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "services/template_service.h"
#include "services/template_manager_service.h"
#include "services/template_fastdfs_service.h"

class TemplateController {
 public:
  /**
   * @param tmpl_mgr_service    可为 nullptr（兼容旧逻辑）；
   *   非 nullptr 时 List 接口将只返回已上架且在有效期内的模板。
   * @param tmpl_fastdfs_service 可为 nullptr；
   *   非 nullptr 且 FastDFS 已启用时，Download/Preview 优先重定向到 FastDFS URL。
   */
  TemplateController(std::shared_ptr<TemplateService>        service,
                     std::shared_ptr<TemplateManagerService> tmpl_mgr_service = nullptr,
                     std::shared_ptr<TemplateFastDfsService> tmpl_fastdfs_service = nullptr);

  HttpResponse List(const HttpRequest& request);
  HttpResponse Download(const HttpRequest& request);
  HttpResponse Preview(const HttpRequest& request);

 private:
  static nlohmann::json ToJson(const RemoteTemplate& item);

  std::shared_ptr<TemplateService>        service_;
  std::shared_ptr<TemplateManagerService> tmpl_mgr_service_;
  std::shared_ptr<TemplateFastDfsService> tmpl_fastdfs_service_;
};
