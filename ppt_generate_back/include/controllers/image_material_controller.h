#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/image_material_service.h"
#include "utils/thread_pool.h"

/**
 * ImageMaterialController — 图片素材管理 REST 接口
 *
 * POST  /api/material/image/upload   — 上传图片（multipart/form-data，字段名 "file"）
 * GET   /api/material/image/list     — 列出当前用户的图片素材
 * GET   /api/material/image/status   — 查询单条图片状态（?id=xxx）
 * GET   /api/material/image/file     — 下载/预览图片文件（?id=xxx）
 * DELETE /api/material/image         — 删除图片素材（?id=xxx）
 */
class ImageMaterialController {
 public:
  ImageMaterialController(std::shared_ptr<AuthService> auth_service,
                          std::shared_ptr<ImageMaterialService> image_material_service,
                          std::shared_ptr<ThreadPool> thread_pool,
                          std::uint64_t max_image_size_mb = 20,
                          const std::vector<std::string>& allowed_types = {"jpg","jpeg","png","webp","gif"});

 HttpResponse Upload(const HttpRequest& request);
  HttpResponse BatchUpload(const HttpRequest& request);
  HttpResponse List(const HttpRequest& request);
  HttpResponse GetStatus(const HttpRequest& request);
  HttpResponse GetFile(const HttpRequest& request);
  HttpResponse Delete(const HttpRequest& request);
  HttpResponse BatchDelete(const HttpRequest& request);

 private:
  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<ImageMaterialService> image_material_service_;
  std::shared_ptr<ThreadPool> thread_pool_;
  std::uint64_t max_image_size_mb_;
  std::vector<std::string> allowed_types_;
};
