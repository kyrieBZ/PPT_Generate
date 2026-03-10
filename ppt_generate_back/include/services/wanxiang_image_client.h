#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_config.h"

class WanxiangImageClient {
 public:
  explicit WanxiangImageClient(ProviderConfig config);

  bool IsEnabled() const;
  std::uint32_t timeout_seconds() const;

  /** 使用通义万象（DashScope）根据提示词生成图片URL列表。 */
  bool GenerateImages(const std::string& prompt,
                      std::vector<std::string>& out_urls,
                      std::string& error_message) const;

 private:
  ProviderConfig config_;
};

