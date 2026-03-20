#pragma once

#include <memory>
#include <string>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/template_service.h"

/**
 * OfficePLUS 模板导入控制器（管理员专用）
 *
 * GET  /api/admin/officeplus/search   — 搜索 OfficePLUS 模板列表
 * GET  /api/admin/officeplus/info     — 获取单个模板详情（预览）
 * POST /api/admin/officeplus/import   — 导入模板（下载 pptx 并写入 catalog）
 * POST /api/admin/officeplus/reload   — 重新加载 catalog（热重载，无需重启服务）
 */
class OfficePlusController {
 public:
  OfficePlusController(std::shared_ptr<AuthService>    auth_service,
                       std::shared_ptr<TemplateService> template_service,
                       const std::string&               python_binary,
                       const std::string&               catalog_path,
                       const std::string&               templates_dir,
                       const std::string&               thumbnails_dir,
                       const std::string&               fetcher_script);

  HttpResponse Search(const HttpRequest& request);
  HttpResponse Info(const HttpRequest& request);
  HttpResponse Import(const HttpRequest& request);
  HttpResponse UploadFile(const HttpRequest& request);
  HttpResponse BatchUpload(const HttpRequest& request);
  HttpResponse Reload(const HttpRequest& request);

 private:
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request, std::string& error) const;

  /** 调用 Python 脚本并返回 stdout JSON 字符串，失败时 error 非空。 */
  std::string RunFetcher(const std::vector<std::string>& args, std::string& error) const;

  std::shared_ptr<AuthService>    auth_service_;
  std::shared_ptr<TemplateService> template_service_;
  std::string python_binary_;
  std::string catalog_path_;
  std::string templates_dir_;
  std::string thumbnails_dir_;
  std::string fetcher_script_;
};
