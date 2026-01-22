#pragma once

#include "services/ppt_service_interface.h"
#include <string>
#include <memory>

/**
 * 基于Aspose.Slides for C++的PowerPoint操作实现
 */
class AsposePowerPointService : public IPowerPointService {
public:
    AsposePowerPointService();
    virtual ~AsposePowerPointService();

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
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class AsposePowerPointServiceFactory : public IPowerPointServiceFactory {
public:
    std::unique_ptr<IPowerPointService> CreateService() override;
};