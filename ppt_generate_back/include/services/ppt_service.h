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
  std::vector<PptRequest> GetHistory(std::uint64_t user_id, const std::string& query, std::string& error);

  // Get all PPT generation history for admin
  std::vector<PptRequest> GetAdminHistory(const std::string& query, std::string& error);

  struct AdminMetricsSeries {
    std::string name;
    std::vector<int> values;
  };

  struct AdminMetrics {
    int total = 0;
    int success = 0;
    int failed = 0;
    int success_rate = 0;
    int unique_users = 0;
    int template_count = 0;
    std::vector<std::string> activity_labels;
    std::vector<int> activity_values;
    std::vector<std::string> generation_labels;
    std::vector<AdminMetricsSeries> generation_series;
    std::vector<std::string> template_labels;
    std::vector<int> template_values;
    std::vector<std::string> region_labels;
    std::vector<int> region_values;
    std::vector<std::string> module_labels;
    std::vector<int> module_values;
  };

  // Get admin dashboard metrics (time range: day/week/month)
  bool GetAdminMetrics(const std::string& range, AdminMetrics& out, std::string& error);

  // Delete a PPT generation request record
  bool DeleteRequest(std::uint64_t user_id, std::uint64_t request_id, std::string& error);

  // Get a single PPT generation request
  bool GetRequest(std::uint64_t user_id, std::uint64_t request_id, PptRequest& out_request, std::string& error);

  // Update output path and status for a request
  bool UpdateRequestOutput(std::uint64_t request_id,
                          std::uint64_t user_id,
                          const std::string& output_path,
                          const std::string& status,
                          std::string& error);

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
