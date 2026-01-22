#pragma once

#include <string>

#include "app_config.h"

class EmailService {
 public:
  explicit EmailService(EmailConfig config);

  bool IsEnabled() const;

  bool Send(const std::string& to_email,
            const std::string& subject,
            const std::string& content,
            std::string& error_message) const;

 private:
  EmailConfig config_;
};
