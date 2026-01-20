#pragma once

#include "services/ppt_service_interface.h"

#include <memory>
#include <string>
#include <vector>

struct LibreOfficeRuntimeOptions {
    std::string python_binary = "python3";
    std::string builder_script;
    std::string soffice_binary = "soffice";
};

/**
 * 基于LibreOffice SDK的PowerPoint操作实现
 */
class LibreOfficePowerPointService : public IPowerPointService {
public:
    explicit LibreOfficePowerPointService(LibreOfficeRuntimeOptions options);
    ~LibreOfficePowerPointService() override;

    bool CreateFromTemplate(const std::string& template_path, 
                           const std::string& output_path) override;

    bool AddSlide(const std::string& ppt_path, 
                 const SlideContent& slide_content, 
                 const std::string& layout_id) override;

    bool ApplyTheme(const std::string& ppt_path,
                   const std::string& primary_color,
                   const std::string& secondary_color,
                   const std::string& accent_color) override;

    bool Save(const std::string& ppt_path) override;

private:
    bool EnsurePathsReady(std::string& error) const;

    LibreOfficeRuntimeOptions options_;
    std::string template_path_;
    std::string output_path_;
    std::string primary_color_;
    std::string secondary_color_;
    std::string accent_color_;
    std::vector<SlideContent> slides_;
};

class LibreOfficePowerPointServiceFactory : public IPowerPointServiceFactory {
public:
    explicit LibreOfficePowerPointServiceFactory(LibreOfficeRuntimeOptions options);
    std::unique_ptr<IPowerPointService> CreateService() override;

private:
    LibreOfficeRuntimeOptions options_;
};
