#pragma once

#include <string>
#include <vector>

#include "app_config.h"

struct DoubaoImageAsset {
  std::string url;
  std::string b64_json;
};

class DoubaoImageClient {
 public:
  explicit DoubaoImageClient(ProviderConfig config);

  bool IsEnabled() const;
  std::uint32_t timeout_seconds() const;
  bool GenerateImages(const std::string& prompt,
                      std::vector<DoubaoImageAsset>& out_assets,
                      std::string& error_message) const;

 private:
  ProviderConfig config_;
};
