#pragma once

#include <cstdint>
#include <string>

#include "app_config.h"

class S3Client {
 public:
  explicit S3Client(S3Config config);

  bool IsEnabled() const;
  std::uint32_t UrlExpirationSeconds() const;

  bool UploadFile(const std::string& local_path,
                  const std::string& object_key,
                  std::string& error) const;

  bool DeleteObject(const std::string& object_key,
                    std::string& error) const;

  std::string PresignGetUrl(const std::string& object_key) const;

 private:
  std::string PresignUrl(const std::string& method,
                         const std::string& object_key,
                         std::uint32_t expires_seconds,
                         const std::string& endpoint_override) const;

  S3Config config_;
};
