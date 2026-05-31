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

  // ── 偏好洞察数据结构 ─────────────────────────────────────────────────────
  struct TopicKeyword {
    std::string keyword;
    int count = 0;
  };

  struct ModelUsage {
    std::string model;
    int count = 0;
  };

  struct HeatmapCell {
    int hour = 0;      // 0-23
    int weekday = 0;   // 0=Mon … 6=Sun
    int count = 0;
  };

  struct InsightData {
    std::vector<TopicKeyword> top_topics;
    std::vector<ModelUsage> model_usage;
    std::vector<HeatmapCell> hourly_heatmap;
    // 用户漏斗
    int funnel_registered = 0;
    int funnel_generated_once = 0;
    int funnel_generated_multi = 0;
    // 页数分布
    std::vector<std::string> pages_labels;
    std::vector<int> pages_values;
  };

  // Get insights data for admin (不需要 range，覆盖全量历史)
  bool GetInsights(InsightData& out, std::string& error);

  // Get all raw topic strings for LLM keyword extraction (at most max_count rows)
  bool GetAllTopics(std::vector<std::string>& out_topics, int max_count, std::string& error);

  // Delete a PPT generation request record
  bool DeleteRequest(std::uint64_t user_id, std::uint64_t request_id, std::string& error);

  // Batch delete PPT records (user can only delete their own; admin can pass user_id=0 to skip check)
  // Returns the count of successfully deleted records; failed ids are collected in out_failed_ids
  int BatchDeleteRequests(std::uint64_t user_id,
                          const std::vector<std::uint64_t>& request_ids,
                          std::vector<std::uint64_t>& out_failed_ids,
                          std::string& error);

  // Get a single PPT generation request
  bool GetRequest(std::uint64_t user_id, std::uint64_t request_id, PptRequest& out_request, std::string& error);

  // Get multiple PPT requests by id list (only returns rows belonging to user_id, silently skips others)
  std::vector<PptRequest> GetRequestsByIds(std::uint64_t user_id,
                                           const std::vector<std::uint64_t>& ids,
                                           std::string& error);

  // Update output path and status for a request
  bool UpdateRequestOutput(std::uint64_t request_id,
                          std::uint64_t user_id,
                          const std::string& output_path,
                          const std::string& status,
                          std::string& error);

  bool UpdateRequestOutput(std::uint64_t request_id,
                          std::uint64_t user_id,
                          const std::string& output_path,
                          const std::string& status,
                          int pages,
                          std::string& error);

  // Set PowerPoint service factory
  void SetPowerPointServiceFactory(std::shared_ptr<IPowerPointServiceFactory> factory);

  // Generate final PPTX file using template and content
  // layout_guide_json: optional JSON array from template analysis, used by builder to pick layout per slide
  // options_json: optional e.g. {"themePreset":"midnight"} for PptxGenJS builder
  bool GeneratePptxFile(const std::string& template_path,
                       const std::vector<SlideContent>& slides,
                       const std::string& output_path,
                       std::string& error,
                       const std::string& layout_guide_json = "",
                       const std::string& options_json = "");

 private:
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<IPowerPointServiceFactory> powerpoint_factory_;
};
