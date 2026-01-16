#pragma once

#include <memory>
#include <string>
#include <vector>
#include "models/user.h"
#include "models/ppt_request.h"
#include "models/slide_content.h"
#include "database/mysql_connection_pool.h"
#include "services/model_service.h"
#include "services/ppt_service_interface.h"  // 包含IPowerPointServiceFactory的完整定义

class PptService {
 public:
  explicit PptService(std::shared_ptr<MySQLConnectionPool> pool);

  // Create a new PPT generation request record
  bool CreateRequest(const PptRequestInput& input, 
                    std::uint64_t user_id, 
                    const std::string& model_name,
                    const std::string& template_name,
                    PptRequest& out_request, 
                    std::string& error);

  // Get user's PPT generation history
  std::vector<PptRequest> GetHistory(std::uint64_t user_id, std::string& error);

  // Delete a PPT generation request record
  bool DeleteRequest(std::uint64_t user_id, std::uint64_t request_id, std::string& error);

  // Set PowerPoint service factory
  void SetPowerPointServiceFactory(std::shared_ptr<IPowerPointServiceFactory> factory);

  // Generate final PPTX file using template and content
  bool GeneratePptxFile(const std::string& template_path,
                       const std::vector<SlideContent>& slides,
                       const std::string& output_path,
                       std::string& error);

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<IPowerPointServiceFactory> powerpoint_factory_;
};
