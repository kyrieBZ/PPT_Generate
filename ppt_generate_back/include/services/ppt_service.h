#pragma once

#include <memory>
#include <string>
#include <vector>

#include "database/mysql_connection_pool.h"
#include "models/ppt_request.h"

class PptService {
 public:
  explicit PptService(std::shared_ptr<MySQLConnectionPool> pool);

  bool CreateRequest(const PptRequestInput& input,
                     std::uint64_t user_id,
                     const std::string& model_name,
                     const std::string& template_name,
                     PptRequest& out_request,
                     std::string& error_message);

  std::vector<PptRequest> GetHistory(std::uint64_t user_id, std::string& error_message) const;
  bool DeleteRequest(std::uint64_t user_id, std::uint64_t request_id, std::string& error_message);

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
};
