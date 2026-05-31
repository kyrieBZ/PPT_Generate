#pragma once

#include <memory>
#include <string>

#include "app_config.h"
#include "http/http_types.h"
#include "services/auth_service.h"

/**
 * VoiceController — 语音识别接口
 *
 * POST /api/voice/transcribe
 *   multipart/form-data: audio 文件字段
 *   → 调用阿里云 NLS 实时转写 HTTP API
 *   → 返回 { "text": "识别结果" }
 */
class VoiceController {
 public:
  VoiceController(std::shared_ptr<AuthService> auth_service,
                  const AsrConfig& asr_config);

  HttpResponse Transcribe(const HttpRequest& request);

 private:
  std::shared_ptr<AuthService> auth_service_;
  AsrConfig asr_config_;

  std::string ExtractToken(const HttpRequest& request);

  /** 解析 multipart/form-data，提取 audio 字段的原始字节 */
  bool ParseMultipartAudio(const HttpRequest& request,
                           std::string& audio_data,
                           std::string& content_type,
                           std::string& error);

  /** 调用阿里云 NLS Flash 语音识别 HTTP API，返回识别文本 */
  bool CallAliyunAsr(const std::string& audio_data,
                     const std::string& audio_format,
                     std::string& result_text,
                     std::string& error);
};
