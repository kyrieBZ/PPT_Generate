#include "services/email_service.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include "logger.h"

namespace {
struct PayloadSource {
  std::string data;
  std::size_t offset = 0;
};

size_t ReadPayload(char* buffer, size_t size, size_t nmemb, void* userp) {
  auto* payload = static_cast<PayloadSource*>(userp);
  const auto max_size = size * nmemb;
  if (!payload || payload->offset >= payload->data.size()) {
    return 0;
  }
  const auto remaining = payload->data.size() - payload->offset;
  const auto to_copy = remaining < max_size ? remaining : max_size;
  std::memcpy(buffer, payload->data.data() + payload->offset, to_copy);
  payload->offset += to_copy;
  return to_copy;
}

std::string BuildMessage(const EmailConfig& config,
                         const std::string& to_email,
                         const std::string& subject,
                         const std::string& content) {
  std::ostringstream stream;
  stream << "From: " << config.from_name << " <" << config.from_email << ">\r\n";
  stream << "To: <" << to_email << ">\r\n";
  stream << "Subject: " << subject << "\r\n";
  stream << "Content-Type: text/plain; charset=UTF-8\r\n";
  stream << "\r\n";
  stream << content << "\r\n";
  return stream.str();
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

enum class SmtpSecurity {
  kNone,
  kStartTls,
  kSmtps
};

SmtpSecurity ResolveSecurity(const EmailConfig& config) {
  if (!config.smtp_security.empty()) {
    const auto security = ToLower(config.smtp_security);
    if (security == "starttls") {
      return SmtpSecurity::kStartTls;
    }
    if (security == "smtps" || security == "ssl") {
      return SmtpSecurity::kSmtps;
    }
    if (security == "none" || security == "plain") {
      return SmtpSecurity::kNone;
    }
  }

  if (!config.use_tls) {
    return SmtpSecurity::kNone;
  }
  if (config.smtp_port == 465) {
    return SmtpSecurity::kSmtps;
  }
  return SmtpSecurity::kStartTls;
}
}

EmailService::EmailService(EmailConfig config) : config_(std::move(config)) {}

bool EmailService::IsEnabled() const {
  return !config_.smtp_host.empty() && !config_.from_email.empty();
}

bool EmailService::Send(const std::string& to_email,
                        const std::string& subject,
                        const std::string& content,
                        std::string& error_message) const {
  if (!IsEnabled()) {
    error_message = "邮件服务未配置";
    return false;
  }
  if (to_email.empty()) {
    error_message = "收件人邮箱为空";
    return false;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "无法初始化邮件客户端";
    return false;
  }

  const auto security = ResolveSecurity(config_);
  std::ostringstream url;
  url << (security == SmtpSecurity::kSmtps ? "smtps://" : "smtp://")
      << config_.smtp_host << ":" << config_.smtp_port;

  PayloadSource payload{BuildMessage(config_, to_email, subject, content), 0};

  struct curl_slist* recipients = nullptr;
  recipients = curl_slist_append(recipients, to_email.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
  curl_easy_setopt(curl, CURLOPT_MAIL_FROM, config_.from_email.c_str());
  curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
  curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadPayload);
  curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
  curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

  if (!config_.smtp_user.empty()) {
    curl_easy_setopt(curl, CURLOPT_USERNAME, config_.smtp_user.c_str());
  }
  if (!config_.smtp_password.empty()) {
    curl_easy_setopt(curl, CURLOPT_PASSWORD, config_.smtp_password.c_str());
  }

  if (security != SmtpSecurity::kNone) {
    curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    error_message = curl_easy_strerror(res);
    Logger::Warn("发送邮件失败: " + error_message);
  }

  curl_slist_free_all(recipients);
  curl_easy_cleanup(curl);

  return res == CURLE_OK;
}
