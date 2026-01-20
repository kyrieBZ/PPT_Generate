#pragma once

#include <memory>
#include <string>
#include <vector>

#include "models/slide_content.h"

/**
 * PowerPoint文件操作接口
 * 用于将生成的内容应用到PowerPoint模板中
 */
class IPowerPointService {
public:
    virtual ~IPowerPointService() = default;

    /**
     * 从模板创建新的PowerPoint文件
     * @param template_path 模板文件路径
     * @param output_path 输出文件路径
     */
    virtual bool CreateFromTemplate(const std::string& template_path, 
                                   const std::string& output_path) = 0;

    /**
     * 添加幻灯片到PowerPoint文件
     * @param ppt_path PowerPoint文件路径
     * @param slide_content 幻灯片内容
     * @param layout_id 布局ID
     */
    virtual bool AddSlide(const std::string& ppt_path, 
                         const SlideContent& slide_content, 
                         const std::string& layout_id) = 0;

    /**
     * 应用主题到PowerPoint文件
     * @param ppt_path PowerPoint文件路径
     * @param primary_color 主色调
     * @param secondary_color 辅助色
     * @param accent_color 强调色
     */
    virtual bool ApplyTheme(const std::string& ppt_path,
                           const std::string& primary_color,
                           const std::string& secondary_color,
                           const std::string& accent_color) = 0;

    /**
     * 保存PowerPoint文件
     * @param ppt_path PowerPoint文件路径
     */
    virtual bool Save(const std::string& ppt_path) = 0;
};

// 抽象工厂接口，用于创建PowerPoint服务实例
class IPowerPointServiceFactory {
public:
    virtual ~IPowerPointServiceFactory() = default;
    
    virtual std::unique_ptr<IPowerPointService> CreateService() = 0;
};
