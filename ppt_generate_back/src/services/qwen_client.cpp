#include "services/qwen_client.h"

#include <curl/curl.h>

#include <sstream>
#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "logger.h"

namespace {
constexpr const char* kQwenEndpoint =
    "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
constexpr int kDefaultTitleMaxChars = 18;
constexpr int kDefaultGroupMaxBullets = 4;
constexpr int kDefaultGroupMaxChars = 28;
constexpr int kMinTitleMaxChars = 4;
constexpr int kMaxTitleMaxChars = 36;
constexpr int kMinGroupChars = 6;
constexpr int kMaxGroupChars = 60;
constexpr int kMaxGroupCount = 4;
constexpr int kMaxBulletsPerGroup = 8;

std::string UrlEncode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      encoded.push_back('+');
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[c >> 4]);
      encoded.push_back(hex[c & 0x0F]);
    }
  }
  return encoded;
}

std::string BuildImageUrl(const std::string& prompt) {
  if (prompt.empty()) {
    return {};
  }
  return "https://source.unsplash.com/featured/960x540/?" + UrlEncode(prompt);
}

std::string ExtractTextFromChoice(const nlohmann::json& choice) {
  if (choice.is_object()) {
    if (auto it = choice.find("message"); it != choice.end() && it->is_object()) {
      if (auto content = it->find("content"); content != it->end() && content->is_string()) {
        return content->get<std::string>();
      }
    }
    if (auto it = choice.find("text"); it != choice.end() && it->is_string()) {
      return it->get<std::string>();
    }
  }
  return {};
}

std::string ExtractTextFromResults(const nlohmann::json& results) {
  if (!results.is_array()) {
    return {};
  }
  for (const auto& result : results) {
    if (!result.is_object()) {
      continue;
    }
    if (auto it = result.find("text"); it != result.end() && it->is_string()) {
      return it->get<std::string>();
    }
    if (auto it = result.find("output_text"); it != result.end() && it->is_string()) {
      return it->get<std::string>();
    }
    if (auto it = result.find("content"); it != result.end() && it->is_string()) {
      return it->get<std::string>();
    }
  }
  return {};
}

std::string ExtractTextFromResponse(const nlohmann::json& response_json) {
  if (auto output = response_json.find("output"); output != response_json.end()) {
    if (auto it = output->find("text"); it != output->end() && it->is_string()) {
      return it->get<std::string>();
    }
    if (auto it = output->find("choices"); it != output->end() && it->is_array()) {
      for (const auto& choice : *it) {
        auto text = ExtractTextFromChoice(choice);
        if (!text.empty()) {
          return text;
        }
      }
    }
    if (auto it = output->find("results"); it != output->end()) {
      auto text = ExtractTextFromResults(*it);
      if (!text.empty()) {
        return text;
      }
    }
  }
  if (auto it = response_json.find("output_text"); it != response_json.end() && it->is_string()) {
    return it->get<std::string>();
  }
  if (auto it = response_json.find("text"); it != response_json.end() && it->is_string()) {
    return it->get<std::string>();
  }
  return {};
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total_size = size * nmemb;
  auto* buffer = static_cast<std::string*>(userp);
  buffer->append(static_cast<char*>(contents), total_size);
  return total_size;
}

void AppendImagePlaceholders(SlideContent& slide,
                             const std::vector<std::string>& prompts,
                             bool include_images) {
  if (!include_images) {
    return;
  }
  for (const auto& prompt : prompts) {
    if (prompt.empty()) {
      continue;
    }
    slide.image_prompts.push_back(prompt);
    const auto url = BuildImageUrl(prompt);
    if (!url.empty()) {
      slide.image_urls.push_back(url);
    }
  }
}

SlideContent ParseSlide(const std::string& text,
                        const std::string& fallback_prompt,
                        bool include_images) {
  SlideContent slide;
  slide.raw_text = text;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    if (slide.title.empty()) {
      slide.title = line;
    } else {
      slide.bullets.push_back(line);
    }
  }
  if (slide.title.empty()) {
    slide.title = "自动生成的PPT";
  }
  if (slide.bullets.empty()) {
    slide.bullets.push_back(text);
  }
  AppendImagePlaceholders(slide, {fallback_prompt}, include_images);
  return slide;
}

std::string BuildOutlinePrompt(const std::string& topic,
                               int slide_count,
                               const std::string& template_hint) {
  std::ostringstream prompt;
  prompt << "你是一名资深中文PPT策划师，请围绕主题【" << topic << "】设计PPT大纲，"
         << "共" << slide_count << "页。";
  if (!template_hint.empty()) {
    prompt << "模板风格参考：" << template_hint << "。";
  }
  prompt << "输出严格的JSON数组，数组中每个元素包含字段："
         << "title（字符串，<=18个汉字），"
         << "key_points（长度2-4的字符串数组，单条<=25字），"
         << "summary（字符串，<=40字，用于概括该页目的）。"
         << "禁止输出除JSON以外的任何字符。";
  return prompt.str();
}

std::string BuildLayoutGuidePrompt(const std::string& template_summary_json,
                                   int slide_count,
                                   const std::string& template_hint) {
  std::ostringstream prompt;
  prompt << "你是一名PPT版式分析师。根据给定的模板结构摘要JSON，输出版式约束。"
         << "目标页数为" << slide_count << "页。";
  if (!template_hint.empty()) {
    prompt << "模板风格参考：" << template_hint << "。";
  }
  prompt << "模板结构摘要JSON如下：\n"
         << template_summary_json << "\n";
  prompt << "请输出严格的JSON数组，数组长度等于目标页数。"
         << "每个元素包含字段："
         << "title_max_chars（整数，标题最大字数，建议8-20），"
         << "groups（数组，正文分组，数组长度代表正文分栏数，至少1）。"
         << "groups中的每个元素包含：max_bullets（2-5），max_chars（单条最大字数，<=35）。"
         << "如模板有图片区域，可适当减少max_bullets。"
         << "禁止输出除JSON以外的任何字符。";
  return prompt.str();
}

std::string BuildSlidesPromptFromOutline(const std::string& topic,
                                         const std::vector<OutlineItem>& outline,
                                         bool include_images) {
  std::ostringstream outline_text;
  for (std::size_t i = 0; i < outline.size(); ++i) {
    outline_text << (i + 1) << ". " << outline[i].title;
    if (!outline[i].summary.empty()) {
      outline_text << "（" << outline[i].summary << "）";
    }
    if (!outline[i].key_points.empty()) {
      outline_text << " 关键点：";
      for (std::size_t j = 0; j < outline[i].key_points.size(); ++j) {
        if (j > 0) {
          outline_text << "；";
        }
        outline_text << outline[i].key_points[j];
      }
    }
    outline_text << "\n";
  }

  std::ostringstream prompt;
  prompt << "你是一名资深中文PPT设计专家，请根据以下大纲为主题【" << topic << "】"
         << "生成每页PPT内容：\n"
         << outline_text.str();
  if (include_images) {
    prompt << "每页需要1-2个图片创意描述，突出场景、风格或配色，供后续图片检索使用。";
  }
  prompt << "输出严格的JSON数组，数组中每个元素包含字段："
         << "title（字符串，需与大纲对应），"
         << "bullets（长度3-5的字符串数组，单条<=40字），"
         << "image_prompts（字符串数组，描述建议配图主题，若无图片需求则给空数组）。"
         << "禁止输出除JSON以外的任何字符。";
  return prompt.str();
}

std::string BuildSlidesPromptFromOutlineWithLayout(const std::string& topic,
                                                   const std::vector<OutlineItem>& outline,
                                                   bool include_images,
                                                   const std::string& layout_guide_json) {
  std::ostringstream outline_text;
  for (std::size_t i = 0; i < outline.size(); ++i) {
    outline_text << (i + 1) << ". " << outline[i].title;
    if (!outline[i].summary.empty()) {
      outline_text << "（" << outline[i].summary << "）";
    }
    if (!outline[i].key_points.empty()) {
      outline_text << " 关键点：";
      for (std::size_t j = 0; j < outline[i].key_points.size(); ++j) {
        if (j > 0) {
          outline_text << "；";
        }
        outline_text << outline[i].key_points[j];
      }
    }
    outline_text << "\n";
  }

  std::ostringstream prompt;
  prompt << "你是一名资深中文PPT设计专家，请根据以下大纲与版式约束为主题【" << topic << "】"
         << "生成每页PPT内容：\n"
         << outline_text.str();
  prompt << "版式约束 layout_guide(JSON数组，索引对应页码，从0开始)如下：\n"
         << layout_guide_json << "\n";
  if (include_images) {
    prompt << "每页需要1-2个图片创意描述，突出场景、风格或配色，供后续图片检索使用。";
  }
  prompt << "输出严格的JSON数组，数组中每个元素包含字段："
         << "title（字符串，需与大纲对应），"
         << "bullet_groups（数组，长度需与layout_guide中groups长度一致；"
         << "每个分组是字符串数组，条数<=对应max_bullets，单条字数<=对应max_chars），"
         << "bullets（可选，flatten后的要点数组，单条<=40字），"
         << "image_prompts（字符串数组，描述建议配图主题，若无图片需求则给空数组）。"
         << "禁止输出除JSON以外的任何字符。";
  return prompt.str();
}

std::string BuildSlidesPromptWithLayout(const std::string& topic,
                                        int slide_count,
                                        const std::string& template_hint,
                                        bool include_images,
                                        const std::string& layout_guide_json) {
  std::ostringstream prompt;
  prompt << "你是一名资深中文PPT设计专家，请围绕主题【" << topic << "】"
         << "策划" << slide_count << "页结构化PPT。";
  if (!template_hint.empty()) {
    prompt << "模板风格参考：" << template_hint << "。";
  }
  prompt << "版式约束 layout_guide(JSON数组，索引对应页码，从0开始)如下：\n"
         << layout_guide_json << "\n";
  if (include_images) {
    prompt << "每页需要1-2个图片创意描述，突出场景、风格或配色，供后续图片检索使用。";
  }
  prompt << "输出严格的JSON数组，数组中每个元素包含字段："
         << "title（字符串，<=18个汉字），"
         << "bullet_groups（数组，长度需与layout_guide中groups长度一致；"
         << "每个分组是字符串数组，条数<=对应max_bullets，单条字数<=对应max_chars），"
         << "bullets（可选，flatten后的要点数组，单条<=40字），"
         << "image_prompts（字符串数组，描述建议配图主题，若无图片需求则给空数组）。"
         << "禁止输出除JSON以外的任何字符。";
  return prompt.str();
}

bool ParseOutlineJson(const nlohmann::json& data, std::vector<OutlineItem>& out_outline) {
  nlohmann::json outline_json = data;
  if (outline_json.is_object()) {
    if (outline_json.contains("outline") && outline_json["outline"].is_array()) {
      outline_json = outline_json["outline"];
    } else if (outline_json.contains("items") && outline_json["items"].is_array()) {
      outline_json = outline_json["items"];
    }
  }
  if (!outline_json.is_array()) {
    return false;
  }
  out_outline.clear();
  for (const auto& item : outline_json) {
    if (!item.is_object()) {
      continue;
    }
    OutlineItem outline;
    outline.title = item.value("title", "");
    outline.summary = item.value("summary", "");
    if (auto it = item.find("key_points"); it != item.end() && it->is_array()) {
      for (const auto& point : *it) {
        if (point.is_string()) {
          outline.key_points.push_back(point.get<std::string>());
        }
      }
    } else if (auto it = item.find("keyPoints"); it != item.end() && it->is_array()) {
      for (const auto& point : *it) {
        if (point.is_string()) {
          outline.key_points.push_back(point.get<std::string>());
        }
      }
    } else if (auto it = item.find("bullets"); it != item.end() && it->is_array()) {
      for (const auto& point : *it) {
        if (point.is_string()) {
          outline.key_points.push_back(point.get<std::string>());
        }
      }
    }
    if (outline.title.empty()) {
      continue;
    }
    out_outline.push_back(std::move(outline));
  }
  return !out_outline.empty();
}

struct GroupConstraint {
  int max_bullets = kDefaultGroupMaxBullets;
  int max_chars = kDefaultGroupMaxChars;
};

struct SlideConstraint {
  int title_max_chars = kDefaultTitleMaxChars;
  std::vector<GroupConstraint> groups;
};

int ClampInt(int value, int min_value, int max_value) {
  return std::max(min_value, std::min(value, max_value));
}

std::size_t Utf8CharBytes(unsigned char lead) {
  if ((lead & 0x80U) == 0) {
    return 1;
  }
  if ((lead & 0xE0U) == 0xC0U) {
    return 2;
  }
  if ((lead & 0xF0U) == 0xE0U) {
    return 3;
  }
  if ((lead & 0xF8U) == 0xF0U) {
    return 4;
  }
  return 1;
}

std::string NormalizeSingleLine(std::string text) {
  for (char& ch : text) {
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      ch = ' ';
    }
  }
  const auto begin = text.find_first_not_of(' ');
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(' ');
  return text.substr(begin, end - begin + 1);
}

std::string Utf8Truncate(const std::string& text, int max_chars) {
  if (max_chars <= 0 || text.empty()) {
    return {};
  }
  std::size_t index = 0;
  int count = 0;
  const auto size = text.size();
  while (index < size && count < max_chars) {
    const auto bytes = Utf8CharBytes(static_cast<unsigned char>(text[index]));
    if (index + bytes > size) {
      break;
    }
    index += bytes;
    ++count;
  }
  if (index >= size) {
    return text;
  }
  return text.substr(0, index);
}

bool ParseLayoutGuide(const std::string& layout_guide_json,
                      std::vector<SlideConstraint>& out_constraints,
                      std::string& error_message) {
  out_constraints.clear();
  if (layout_guide_json.empty()) {
    return true;
  }
  try {
    nlohmann::json guide_json = nlohmann::json::parse(layout_guide_json);
    if (guide_json.is_object()) {
      if (guide_json.contains("layout_guide") && guide_json["layout_guide"].is_array()) {
        guide_json = guide_json["layout_guide"];
      } else if (guide_json.contains("layoutGuide") && guide_json["layoutGuide"].is_array()) {
        guide_json = guide_json["layoutGuide"];
      }
    }
    if (!guide_json.is_array()) {
      error_message = "layout_guide 不是数组";
      return false;
    }
    for (const auto& item : guide_json) {
      SlideConstraint constraint;
      if (item.is_object()) {
        constraint.title_max_chars = ClampInt(
            item.value("title_max_chars", item.value("titleMaxChars", kDefaultTitleMaxChars)),
            kMinTitleMaxChars, kMaxTitleMaxChars);

        const nlohmann::json* groups_json = nullptr;
        if (item.contains("groups") && item["groups"].is_array()) {
          groups_json = &item["groups"];
        }
        if (groups_json != nullptr) {
          for (const auto& group_item : *groups_json) {
            if (!group_item.is_object()) {
              continue;
            }
            GroupConstraint group;
            group.max_bullets =
                ClampInt(group_item.value("max_bullets",
                                          group_item.value("maxBullets", kDefaultGroupMaxBullets)),
                         1, kMaxBulletsPerGroup);
            group.max_chars =
                ClampInt(group_item.value("max_chars",
                                          group_item.value("maxChars", kDefaultGroupMaxChars)),
                         kMinGroupChars, kMaxGroupChars);
            constraint.groups.push_back(group);
            if (constraint.groups.size() >= static_cast<std::size_t>(kMaxGroupCount)) {
              break;
            }
          }
        }
      }
      if (constraint.groups.empty()) {
        constraint.groups.push_back(GroupConstraint{});
      }
      out_constraints.push_back(std::move(constraint));
    }
    return true;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}

void RebuildRawText(SlideContent& slide) {
  slide.raw_text = slide.title;
  for (const auto& bullet : slide.bullets) {
    if (!bullet.empty()) {
      slide.raw_text += "\n" + bullet;
    }
  }
}

void ApplySlideConstraint(SlideContent& slide, const SlideConstraint& constraint) {
  slide.title = Utf8Truncate(NormalizeSingleLine(slide.title), constraint.title_max_chars);
  if (slide.title.empty()) {
    slide.title = "自动生成的PPT";
  }

  std::vector<std::string> source;
  for (const auto& group : slide.bullet_groups) {
    for (const auto& item : group) {
      const auto normalized = NormalizeSingleLine(item);
      if (!normalized.empty()) {
        source.push_back(normalized);
      }
    }
  }
  if (source.empty()) {
    for (const auto& item : slide.bullets) {
      const auto normalized = NormalizeSingleLine(item);
      if (!normalized.empty()) {
        source.push_back(normalized);
      }
    }
  }

  std::size_t cursor = 0;
  std::vector<std::vector<std::string>> constrained_groups;
  constrained_groups.reserve(constraint.groups.size());
  for (const auto& group_constraint : constraint.groups) {
    std::vector<std::string> group;
    group.reserve(static_cast<std::size_t>(group_constraint.max_bullets));
    for (int i = 0; i < group_constraint.max_bullets && cursor < source.size(); ++i, ++cursor) {
      auto trimmed = Utf8Truncate(source[cursor], group_constraint.max_chars);
      trimmed = NormalizeSingleLine(trimmed);
      if (!trimmed.empty()) {
        group.push_back(std::move(trimmed));
      }
    }
    if (!group.empty()) {
      constrained_groups.push_back(std::move(group));
    }
  }

  std::size_t dropped_count = 0;
  if (cursor < source.size()) {
    dropped_count = source.size() - cursor;
  }

  if (constrained_groups.empty() && !source.empty()) {
    const int max_chars = constraint.groups.empty() ? kDefaultGroupMaxChars : constraint.groups[0].max_chars;
    auto first = Utf8Truncate(source.front(), max_chars);
    first = NormalizeSingleLine(first);
    if (!first.empty()) {
      constrained_groups.push_back({first});
      dropped_count = source.size() > 1 ? source.size() - 1 : 0;
    }
  }

  slide.bullet_groups = std::move(constrained_groups);
  slide.bullets.clear();
  for (const auto& group : slide.bullet_groups) {
    for (const auto& item : group) {
      slide.bullets.push_back(item);
    }
  }

  if (dropped_count > 0) {
    std::ostringstream note;
    note << "layout_guide裁剪了" << dropped_count << "条超限要点";
    if (slide.notes.empty()) {
      slide.notes = note.str();
    } else {
      slide.notes += "\n" + note.str();
    }
  }

  RebuildRawText(slide);
}

bool ApplyLayoutGuideConstraints(std::vector<SlideContent>& slides,
                                 const std::string& layout_guide_json,
                                 std::string& warning_message) {
  if (layout_guide_json.empty() || slides.empty()) {
    return true;
  }
  std::vector<SlideConstraint> constraints;
  if (!ParseLayoutGuide(layout_guide_json, constraints, warning_message)) {
    return false;
  }
  if (constraints.empty()) {
    return true;
  }
  for (std::size_t i = 0; i < slides.size(); ++i) {
    const auto& constraint =
        constraints[i < constraints.size() ? i : constraints.size() - 1];
    ApplySlideConstraint(slides[i], constraint);
  }
  return true;
}

bool ParseSlidesText(const std::string& slides_text,
                     const std::string& topic,
                     bool include_images,
                     std::vector<SlideContent>& out_slides,
                     std::string& error_message) {
  if (slides_text.empty()) {
    error_message = "通义千问返回内容为空";
    return false;
  }
  try {
    auto response_json = nlohmann::json::parse(slides_text);
    if (response_json.is_object() && response_json.contains("slides")) {
      response_json = response_json["slides"];
    }
    if (!response_json.is_array()) {
      error_message = "返回格式不是数组";
      return false;
    }
    out_slides.clear();
    for (const auto& slide_json : response_json) {
      SlideContent slide;
      slide.title = slide_json.value("title", "");
      std::vector<std::vector<std::string>> bullet_groups;
      if (auto it = slide_json.find("bullet_groups"); it != slide_json.end() && it->is_array()) {
        for (const auto& group : *it) {
          if (!group.is_array()) {
            continue;
          }
          std::vector<std::string> group_items;
          for (const auto& item : group) {
            if (item.is_string()) {
              const auto value = item.get<std::string>();
              if (!value.empty()) {
                group_items.push_back(value);
              }
            }
          }
          if (!group_items.empty()) {
            bullet_groups.push_back(std::move(group_items));
          }
        }
      } else if (auto it = slide_json.find("bulletGroups"); it != slide_json.end() && it->is_array()) {
        for (const auto& group : *it) {
          if (!group.is_array()) {
            continue;
          }
          std::vector<std::string> group_items;
          for (const auto& item : group) {
            if (item.is_string()) {
              const auto value = item.get<std::string>();
              if (!value.empty()) {
                group_items.push_back(value);
              }
            }
          }
          if (!group_items.empty()) {
            bullet_groups.push_back(std::move(group_items));
          }
        }
      }

      if (auto it = slide_json.find("bullets"); it != slide_json.end() && it->is_array()) {
        for (const auto& bullet : *it) {
          slide.bullets.push_back(bullet.get<std::string>());
        }
      }
      if (slide.bullets.empty() && !bullet_groups.empty()) {
        for (const auto& group : bullet_groups) {
          for (const auto& bullet : group) {
            slide.bullets.push_back(bullet);
          }
        }
      }
      slide.bullet_groups = std::move(bullet_groups);
      slide.raw_text = slide.title;
      for (const auto& bullet : slide.bullets) {
        slide.raw_text += "\n" + bullet;
      }
      if (slide.title.empty()) {
        slide = ParseSlide(slide.raw_text, topic + " 配图", include_images);
      }

      std::vector<std::string> prompts;
      if (auto it = slide_json.find("image_prompts"); it != slide_json.end() && it->is_array()) {
        for (const auto& value : *it) {
          if (value.is_string()) {
            prompts.push_back(value.get<std::string>());
          }
        }
      } else if (auto single = slide_json.find("image_prompt"); single != slide_json.end() && single->is_string()) {
        prompts.push_back(single->get<std::string>());
      }
      if (prompts.empty() && include_images) {
        prompts.push_back(slide.title.empty() ? topic + " 场景" : slide.title + " 配图");
      }
      AppendImagePlaceholders(slide, prompts, include_images);

      out_slides.push_back(std::move(slide));
    }
    if (out_slides.empty()) {
      error_message = "未能解析任何幻灯片";
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}

bool CallQwen(const std::string& api_key,
              const std::string& prompt,
              std::string& text_out,
              std::string& error_message) {
  if (api_key.empty()) {
    error_message = "未配置通义千问API密钥";
    return false;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "无法初始化HTTP客户端";
    return false;
  }

  nlohmann::json body;
  body["model"] = "qwen-plus";
  body["parameters"]["result_format"] = "json";
  body["input"]["prompt"] = prompt;

  std::string response_buffer;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth_header = "Authorization: Bearer " + api_key;
  headers = curl_slist_append(headers, auth_header.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, kQwenEndpoint);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  const auto payload = body.dump();
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error_message = curl_easy_strerror(res);
    return false;
  }

  try {
    auto response_json = nlohmann::json::parse(response_buffer);
    if (response_json.contains("code") && response_json.contains("message")) {
      error_message = response_json.value("message", "通义千问调用失败");
      return false;
    }
    const auto text = ExtractTextFromResponse(response_json);
    if (text.empty()) {
      error_message = response_json.value("message", "通义千问返回内容为空");
      return false;
    }
    text_out = text;
    return true;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}
}

QwenClient::QwenClient(std::string api_key) : api_key_(std::move(api_key)) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

bool QwenClient::GenerateOutline(const std::string& topic,
                                 int slide_count,
                                 const std::string& template_hint,
                                 std::vector<OutlineItem>& out_outline,
                                 std::string& error_message) const {
  slide_count = std::max(1, std::min(slide_count, 10));
  const auto prompt = BuildOutlinePrompt(topic, slide_count, template_hint);
  std::string outline_text;
  if (!CallQwen(api_key_, prompt, outline_text, error_message)) {
    return false;
  }
  try {
    auto outline_json = nlohmann::json::parse(outline_text);
    if (!ParseOutlineJson(outline_json, out_outline)) {
      error_message = "大纲解析失败";
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}

bool QwenClient::GenerateLayoutGuide(const std::string& template_summary_json,
                                     int slide_count,
                                     const std::string& template_hint,
                                     std::string& out_layout_json,
                                     std::string& error_message) const {
  if (template_summary_json.empty()) {
    error_message = "模板结构摘要为空";
    return false;
  }
  slide_count = std::max(1, std::min(slide_count, 50));
  const auto prompt = BuildLayoutGuidePrompt(template_summary_json, slide_count, template_hint);
  std::string guide_text;
  if (!CallQwen(api_key_, prompt, guide_text, error_message)) {
    return false;
  }
  try {
    auto guide_json = nlohmann::json::parse(guide_text);
    if (guide_json.is_object()) {
      if (guide_json.contains("layout_guide")) {
        guide_json = guide_json["layout_guide"];
      } else if (guide_json.contains("layoutGuide")) {
        guide_json = guide_json["layoutGuide"];
      }
    }
    if (!guide_json.is_array()) {
      error_message = "版式约束解析失败：返回格式不是数组";
      return false;
    }
    if (guide_json.size() > static_cast<std::size_t>(slide_count)) {
      guide_json.erase(guide_json.begin() + slide_count, guide_json.end());
    }
    while (guide_json.size() < static_cast<std::size_t>(slide_count)) {
      if (guide_json.empty()) {
        guide_json.push_back(nlohmann::json::object());
      } else {
        guide_json.push_back(guide_json.back());
      }
    }
    out_layout_json = guide_json.dump();
    return true;
  } catch (const std::exception& ex) {
    error_message = ex.what();
    return false;
  }
}

bool QwenClient::GenerateSlidesFromOutline(const std::string& topic,
                                           const std::vector<OutlineItem>& outline,
                                           bool include_images,
                                           std::vector<SlideContent>& out_slides,
                                           std::string& error_message) const {
  if (outline.empty()) {
    error_message = "大纲为空";
    return false;
  }
  const auto prompt = BuildSlidesPromptFromOutline(topic, outline, include_images);
  std::string slides_text;
  if (!CallQwen(api_key_, prompt, slides_text, error_message)) {
    return false;
  }
  return ParseSlidesText(slides_text, topic, include_images, out_slides, error_message);
}

bool QwenClient::GenerateSlidesFromOutlineWithLayout(const std::string& topic,
                                                     const std::vector<OutlineItem>& outline,
                                                     bool include_images,
                                                     const std::string& layout_guide_json,
                                                     std::vector<SlideContent>& out_slides,
                                                     std::string& error_message) const {
  if (layout_guide_json.empty()) {
    return GenerateSlidesFromOutline(topic, outline, include_images, out_slides, error_message);
  }
  if (outline.empty()) {
    error_message = "大纲为空";
    return false;
  }
  const auto prompt = BuildSlidesPromptFromOutlineWithLayout(topic, outline, include_images,
                                                             layout_guide_json);
  std::string slides_text;
  if (!CallQwen(api_key_, prompt, slides_text, error_message)) {
    return false;
  }
  if (!ParseSlidesText(slides_text, topic, include_images, out_slides, error_message)) {
    return false;
  }
  std::string layout_warning;
  if (!ApplyLayoutGuideConstraints(out_slides, layout_guide_json, layout_warning)) {
    Logger::Warn("layout_guide约束应用失败，将保留原始模型输出: " + layout_warning);
  }
  return true;
}

bool QwenClient::GenerateSlides(const std::string& topic,
                                int slide_count,
                                const std::string& template_hint,
                                bool include_images,
                                std::vector<SlideContent>& out_slides,
                                std::string& error_message) const {
  slide_count = std::max(1, std::min(slide_count, 10));

  std::vector<OutlineItem> outline;
  std::string outline_error;
  if (!GenerateOutline(topic, slide_count, template_hint, outline, outline_error)) {
    Logger::Warn("通义千问大纲生成失败，将回退直出模式: " + outline_error);
    outline.clear();
  }

  if (!outline.empty()) {
    std::string slides_error;
    if (GenerateSlidesFromOutline(topic, outline, include_images, out_slides, slides_error)) {
      return true;
    }
    Logger::Warn("通义千问大纲内容生成失败，将回退直出模式: " + slides_error);
  }

  std::ostringstream prompt;
  prompt << "你是一名资深中文PPT设计专家，请围绕主题【" << topic << "】"
         << "策划" << slide_count << "页结构化PPT。";
  if (!template_hint.empty()) {
    prompt << "模板风格参考：" << template_hint << "。";
  }
  if (include_images) {
    prompt << "每页需要1-2个图片创意描述，突出场景、风格或配色，供后续图片检索使用。";
  }
  prompt << "输出严格的JSON数组，数组中每个元素包含字段："
         << "title（字符串，<=18个汉字），"
         << "bullets（长度3-5的字符串数组，单条<=40字），"
         << "image_prompts（字符串数组，描述建议配图主题，若无图片需求则给空数组）。"
         << "禁止输出除JSON以外的任何字符。";
  std::string slides_text;
  if (!CallQwen(api_key_, prompt.str(), slides_text, error_message)) {
    return false;
  }
  if (ParseSlidesText(slides_text, topic, include_images, out_slides, error_message)) {
    return true;
  }
  Logger::Warn(std::string("解析通义千问JSON失败，将回退文本模式: ") + error_message);
  out_slides.clear();
  out_slides.push_back(ParseSlide(slides_text, topic + " 场景", include_images));
  return true;
}

bool QwenClient::GenerateSlidesWithLayout(const std::string& topic,
                                          int slide_count,
                                          const std::string& template_hint,
                                          bool include_images,
                                          const std::string& layout_guide_json,
                                          std::vector<SlideContent>& out_slides,
                                          std::string& error_message) const {
  if (layout_guide_json.empty()) {
    return GenerateSlides(topic, slide_count, template_hint, include_images, out_slides, error_message);
  }
  slide_count = std::max(1, std::min(slide_count, 10));

  std::ostringstream prompt;
  prompt << BuildSlidesPromptWithLayout(topic, slide_count, template_hint, include_images,
                                        layout_guide_json);
  std::string slides_text;
  if (!CallQwen(api_key_, prompt.str(), slides_text, error_message)) {
    return false;
  }
  if (ParseSlidesText(slides_text, topic, include_images, out_slides, error_message)) {
    std::string layout_warning;
    if (!ApplyLayoutGuideConstraints(out_slides, layout_guide_json, layout_warning)) {
      Logger::Warn("layout_guide约束应用失败，将保留原始模型输出: " + layout_warning);
    }
    return true;
  }
  Logger::Warn(std::string("解析通义千问JSON失败，将回退文本模式: ") + error_message);
  out_slides.clear();
  out_slides.push_back(ParseSlide(slides_text, topic + " 场景", include_images));
  std::string layout_warning;
  if (!ApplyLayoutGuideConstraints(out_slides, layout_guide_json, layout_warning)) {
    Logger::Warn("layout_guide约束应用失败，将保留原始模型输出: " + layout_warning);
  }
  return true;
}
