#pragma once

#include <cstddef>
#include <string>

namespace crypto_utils {

std::string GenerateSalt(std::size_t length = 16);
std::string Sha256(const std::string& input);
std::string HashPassword(const std::string& password, const std::string& salt);
std::string GenerateToken(std::size_t byte_length = 32);

}
