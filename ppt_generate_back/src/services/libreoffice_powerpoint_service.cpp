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

bool LibreOfficePowerPointService::Save(const std::string&) {
  std::string error;
  if (!EnsurePathsReady(error)) {
    Logger::Warn(error);
    return false;
  }

  auto looks_like_zip = [](const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
      return false;
    }
    char signature[2] = {0};
    input.read(signature, 2);
    return input.gcount() == 2 && signature[0] == 'P' && signature[1] == 'K';
  };

  std::filesystem::path payload_path(output_path_);
  payload_path.replace_extension(".json");
  std::error_code ec;
  std::filesystem::create_directories(payload_path.parent_path(), ec);

  std::string template_path_for_script = template_path_;
  std::filesystem::path template_copy(output_path_);
  template_copy.replace_extension(".template.pptx");
  std::error_code copy_ec;
  std::filesystem::copy_file(template_path_, template_copy,
                             std::filesystem::copy_options::overwrite_existing, copy_ec);
  if (!copy_ec) {
    const auto size = std::filesystem::file_size(template_copy, copy_ec);
    if (!copy_ec && size > 0 && looks_like_zip(template_copy)) {
      template_path_for_script = template_copy.string();
    } else {
      Logger::Warn("模板副本无效，将回退原始模板");
    }
  } else {
    Logger::Warn("模板复制失败: " + copy_ec.message());
  }

  nlohmann::json payload;
  payload["theme"] = {
      {"primaryColor", primary_color_},
      {"secondaryColor", secondary_color_},
      {"accentColor", accent_color_}};
  payload["layoutMode"] = "template";
  payload["slides"] = nlohmann::json::array();
  for (const auto& slide : slides_) {
    nlohmann::json item;
    item["title"] = slide.title;
    if (!slide.bullets.empty()) {
      item["bullets"] = slide.bullets;
    } else if (!slide.raw_text.empty()) {
      item["bullets"] = nlohmann::json::array({slide.raw_text});
    }
    if (!slide.notes.empty()) {
      item["notes"] = slide.notes;
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

  std::ostringstream command;
  command << '"' << options_.python_binary << '"'
          << " \"" << options_.builder_script << "\""
          << " --template \"" << template_path_for_script << "\""
          << " --output \"" << output_path_ << "\""
          << " --data-json \"" << payload_path.string() << "\"";

  const int result = std::system(command.str().c_str());
  if (result != 0) {
    Logger::Warn("PPT生成脚本执行失败");
    return false;
  }
  return true;
}

bool LibreOfficePowerPointService::EnsurePathsReady(std::string& error) const {
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
