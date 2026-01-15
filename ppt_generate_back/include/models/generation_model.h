#pragma once

#include <string>
#include <vector>

struct GenerationModel {
  std::string id;
  std::string name;
  std::string provider;
  std::string locale;
  std::string description;
  std::string latency_level;  // e.g., low/medium/high
  std::string cost_level;     // e.g., free/premium
  std::vector<std::string> capabilities;
};
