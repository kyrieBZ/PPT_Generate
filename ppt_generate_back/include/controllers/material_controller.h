#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/material_service.h"
#include "utils/thread_pool.h"

class MaterialController {
 public:
  MaterialController(std::shared_ptr<AuthService> auth_service,
                     std::shared_ptr<MaterialService> material_service,
                     std::shared_ptr<ThreadPool> thread_pool);

  /** POST /api/material/upload  — multipart/form-data */
  HttpResponse Upload(const HttpRequest& request);

  /** GET /api/material/status?id= */
  HttpResponse GetStatus(const HttpRequest& request);

  /** GET /api/material/result?id= */
  HttpResponse GetResult(const HttpRequest& request);

  /** PUT /api/material/result?id= */
  HttpResponse SaveResult(const HttpRequest& request);

  /** GET /api/material/list */
  HttpResponse List(const HttpRequest& request);

  /** DELETE /api/material?id= */
  HttpResponse Delete(const HttpRequest& request);

  /** POST /api/material/batch_upload — multipart/form-data，多文件 */
  HttpResponse BatchUpload(const HttpRequest& request);

  /** GET /api/material/batch_status?ids=id1,id2,... */
  HttpResponse BatchStatus(const HttpRequest& request);

 private:
  std::shared_ptr<User> Authenticate(const HttpRequest& request, std::string& error) const;

  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<MaterialService> material_service_;
  std::shared_ptr<ThreadPool> thread_pool_;
};
