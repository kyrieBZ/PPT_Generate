#include "services/libreoffice_powerpoint_service.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "logger.h"

LibreOfficePowerPointService::LibreOfficePowerPointService(LibreOfficeRuntimeOptions options)
    : options_(std::move(options)) {}

LibreOfficePowerPointService::~LibreOfficePowerPointService() = default;

bool LibreOfficePowerPointService::CreateFromTemplate(const std::string& template_path,
                                                      const std::string& output_path) {
  template_path_ = template_path;
  output_path_ = output_path;
  slides_.clear();
  return true;
}

bool LibreOfficePowerPointService::AddSlide(const std::string&,
                                            const SlideContent& slide_content,
                                            const std::string&) {
  slides_.push_back(slide_content);
  return true;
}

bool LibreOfficePowerPointService::ApplyTheme(const std::string&,
                                              const std::string& primary_color,
                                              const std::string& secondary_color,
                                              const std::string& accent_color) {
  primary_color_ = primary_color;
  secondary_color_ = secondary_color;
  accent_color_ = accent_color;
  return true;
}

bool LibreOfficePowerPointService::Save(const std::string&, const std::string& layout_guide_json,
                                        const std::string& options_json) {
  std::string effective_builder_mode = options_.builder_mode;
  if (!options_json.empty()) {
    try {
      auto opts = nlohmann::json::parse(options_json);
      if (opts.contains("builderMode") && opts["builderMode"].is_string()) {
        effective_builder_mode = opts["builderMode"].get<std::string>();
      }
    } catch (const std::exception&) { /* ignore */ }
  }
  if (effective_builder_mode == "template") {
    effective_builder_mode = "python";
  }
  std::string error;
  if (!EnsurePathsReady(error, effective_builder_mode)) {
    Logger::Warn(error);
    return false;
  }

  std::filesystem::path payload_path(output_path_);
  payload_path.replace_extension(".json");
  std::error_code ec;
  std::filesystem::create_directories(payload_path.parent_path(), ec);

  std::string template_path_for_script = template_path_;

  nlohmann::json payload;
  payload["theme"] = {
      {"primaryColor", primary_color_},
      {"secondaryColor", secondary_color_},
      {"accentColor", accent_color_}};
  payload["layoutMode"] = "template";
  payload["cleanTemplateText"] = true;
  if (!layout_guide_json.empty()) {
    try {
      payload["layout_guide"] = nlohmann::json::parse(layout_guide_json);
    } catch (const std::exception&) {
      /* ignore parse error, builder will fall back to heuristic */
    }
  }
  if (!options_json.empty()) {
    try {
      auto opts = nlohmann::json::parse(options_json);
      if (opts.contains("themePreset") && opts["themePreset"].is_string()) {
        payload["themePreset"] = opts["themePreset"];
      }
    } catch (const std::exception&) {
      /* ignore */
    }
  }
  payload["slides"] = nlohmann::json::array();
  for (const auto& slide : slides_) {
    nlohmann::json item;
    item["title"] = slide.title;
    if (!slide.bullets.empty()) {
      item["bullets"] = slide.bullets;
    } else if (!slide.raw_text.empty()) {
      item["bullets"] = nlohmann::json::array({slide.raw_text});
    }
    if (!slide.bullet_groups.empty()) {
      item["bulletGroups"] = slide.bullet_groups;
    }
    if (!slide.notes.empty()) {
      item["notes"] = slide.notes;
    }
    if (!slide.image_paths.empty()) {
      item["imagePaths"] = slide.image_paths;
    }
    if (!slide.image_urls.empty()) {
      item["imageUrls"] = slide.image_urls;
    }
    if (!slide.image_prompts.empty()) {
      item["imagePrompts"] = slide.image_prompts;
    }
    if (!slide.layout_hint.empty()) {
      item["layoutHint"] = slide.layout_hint;
    }
    payload["slides"].push_back(item);
  }

  std::ofstream output(payload_path);
  if (!output.is_open()) {
    Logger::Warn("无法写入PPT生成数据文件");
    return false;
  }
  output << payload.dump();
  output.close();

  std::error_code ec_abs;
  std::filesystem::path template_abs = std::filesystem::absolute(
      std::filesystem::path(template_path_for_script), ec_abs);
  if (!ec_abs) {
    template_path_for_script = template_abs.string();
  }
  std::string payload_path_abs = payload_path.string();
  std::string output_path_abs = output_path_;
  std::error_code ec_abs2;
  std::filesystem::path payload_abs = std::filesystem::absolute(payload_path, ec_abs2);
  if (!ec_abs2) {
    payload_path_abs = payload_abs.string();
  }
  ec_abs2.clear();
  std::filesystem::path out_abs = std::filesystem::absolute(
      std::filesystem::path(output_path_), ec_abs2);
  if (!ec_abs2) {
    output_path_abs = out_abs.string();
  }

  std::ostringstream command;
  const bool use_pptxgenjs = (effective_builder_mode == "pptxgenjs" &&
                              !options_.node_binary.empty() &&
                              !options_.pptxgen_builder_script.empty() &&
                              std::filesystem::exists(options_.pptxgen_builder_script));
  Logger::Info(std::string("PPT builder: ") + (use_pptxgenjs ? "pptxgenjs" : "python") +
               " template=" + template_path_for_script);
  if (use_pptxgenjs) {
    command << '"' << options_.node_binary << '"'
            << " \"" << options_.pptxgen_builder_script << "\""
            << " --data-json \"" << payload_path_abs << "\""
            << " --output \"" << output_path_abs << "\"";
  } else {
    command << '"' << options_.python_binary << '"'
            << " \"" << options_.builder_script << "\""
            << " --template \"" << template_path_for_script << "\""
            << " --output \"" << output_path_abs << "\""
            << " --data-json \"" << payload_path_abs << "\"";
  }

  const int result = std::system(command.str().c_str());
  if (result != 0) {
    Logger::Warn(use_pptxgenjs ? "PptxGenJS 脚本执行失败" : "PPT生成脚本执行失败");
    return false;
  }
  return true;
}

bool LibreOfficePowerPointService::EnsurePathsReady(std::string& error, const std::string& effective_builder_mode) const {
  const std::string effective = effective_builder_mode.empty() ? options_.builder_mode : effective_builder_mode;
  const bool use_pptxgenjs = (effective == "pptxgenjs" &&
                              !options_.node_binary.empty() &&
                              !options_.pptxgen_builder_script.empty());
  if (use_pptxgenjs) {
    if (!std::filesystem::exists(options_.pptxgen_builder_script)) {
      error = "PptxGenJS 脚本不存在";
      return false;
    }
  } else {
    if (options_.python_binary.empty()) {
      error = "Python执行器未配置";
      return false;
    }
    if (options_.builder_script.empty()) {
      error = "PPT生成脚本未配置";
      return false;
    }
    if (!std::filesystem::exists(options_.builder_script)) {
      error = "PPT生成脚本不存在";
      return false;
    }
    if (template_path_.empty() || !std::filesystem::exists(template_path_)) {
      error = "模板文件不存在";
      return false;
    }
    if (!std::filesystem::is_regular_file(template_path_)) {
      error = "模板文件不可读";
      return false;
    }
  }
  if (output_path_.empty()) {
    error = "输出路径为空";
    return false;
  }
  return true;
}

LibreOfficePowerPointServiceFactory::LibreOfficePowerPointServiceFactory(LibreOfficeRuntimeOptions options)
    : options_(std::move(options)) {}

std::unique_ptr<IPowerPointService> LibreOfficePowerPointServiceFactory::CreateService() {
  return std::make_unique<LibreOfficePowerPointService>(options_);
}
