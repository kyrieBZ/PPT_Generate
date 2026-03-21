#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "models/ppt_template.h"

class TemplateService {
 public:
  explicit TemplateService(const std::string& catalog_path);

  std::vector<RemoteTemplate> GetAll() const {
    std::lock_guard<std::mutex> lock(mu_);
    return templates_;
  }
  std::vector<RemoteTemplate> Search(const std::string& query) const;
  std::optional<RemoteTemplate> FindById(const std::string& id) const;
  std::optional<std::string> GetLocalFile(const std::string& id) const;
  /** Path to preview image (e.g. PNG) for template id, if file exists. */
  std::optional<std::string> GetPreviewPath(const std::string& id) const;

  /** 重新从磁盘加载 catalog（热重载，线程安全）。返回加载后的模板数量。 */
  int Reload(std::string& error);

  /**
   * 从 catalog JSON 文件中移除指定模板并写回磁盘，然后热重载内存副本。
   * 成功返回 true；若 id 不在 catalog 中也返回 true（幂等）。
   * out_local_file：若原条目有 local_file 字段，写入此变量供调用方删除本地文件。
   */
  bool RemoveFromCatalog(const std::string& id,
                         std::string& out_local_file,
                         std::string& error);

  const std::string& CatalogPath() const { return catalog_path_str_; }

 private:
  void LoadCatalog(const std::string& path);

  mutable std::mutex   mu_;
  std::vector<RemoteTemplate> templates_;
  std::filesystem::path catalog_dir_;
  std::string           catalog_path_str_;
};
