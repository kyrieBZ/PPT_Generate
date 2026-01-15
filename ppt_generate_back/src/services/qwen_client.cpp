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
}

QwenClient::QwenClient(std::string api_key) : api_key_(std::move(api_key)) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

bool QwenClient::GenerateSlides(const std::string& topic,
                                int slide_count,
                                const std::string& template_hint,
                                bool include_images,
                                std::vector<SlideContent>& out_slides,
                                std::string& error_message) const {
  if (api_key_.empty()) {
    error_message = "未配置通义千问API密钥";
    return false;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "无法初始化HTTP客户端";
    return false;
  }

  slide_count = std::max(1, std::min(slide_count, 10));

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

  nlohmann::json body;
  body["model"] = "qwen-plus";
  body["parameters"]["result_format"] = "json";
  body["input"]["prompt"] = prompt.str();

  std::string response_buffer;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string auth_header = "Authorization: Bearer " + api_key_;
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
    try {
      auto slides_json = nlohmann::json::parse(text);
      if (!slides_json.is_array()) {
        if (slides_json.is_object() && slides_json.contains("slides") && slides_json["slides"].is_array()) {
          slides_json = slides_json["slides"];
        } else {
          throw std::runtime_error("返回格式不是数组");
        }
      }
      out_slides.clear();
      for (const auto& slide_json : slides_json) {
        SlideContent slide;
        slide.title = slide_json.value("title", "");
        if (auto it = slide_json.find("bullets"); it != slide_json.end() && it->is_array()) {
          for (const auto& bullet : *it) {
            slide.bullets.push_back(bullet.get<std::string>());
          }
        }
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

        out_slides.push_back(slide);
      }
      if (out_slides.empty()) {
        error_message = "未能解析任何幻灯片";
        return false;
      }
      return true;
    } catch (const std::exception& parse_ex) {
      Logger::Warn(std::string("解析通义千问JSON失败，将回退文本模式: ") + parse_ex.what());
      out_slides.clear();
      out_slides.push_back(ParseSlide(text, topic + " 场景", include_images));
      return true;
    }
  } catch (const std::exception& ex) {
    error_message = ex.what();
  }
  return false;
}
