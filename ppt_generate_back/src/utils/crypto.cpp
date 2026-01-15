#include "utils/crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <stdexcept>

namespace {
std::string BytesToHex(const unsigned char* data, std::size_t length) {
  static constexpr char kHexChars[] = "0123456789abcdef";
  std::string output;
  output.resize(length * 2);
  for (std::size_t i = 0; i < length; ++i) {
    output[i * 2] = kHexChars[(data[i] >> 4) & 0x0F];
    output[i * 2 + 1] = kHexChars[data[i] & 0x0F];
  }
  return output;
}

std::string RandomBytes(std::size_t length) {
  std::string buffer(length, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data()), static_cast<int>(length)) != 1) {
    throw std::runtime_error("Unable to generate secure random bytes");
  }
  return buffer;
}
}

namespace crypto_utils {

std::string GenerateSalt(std::size_t length) {
  if (length == 0) {
    length = 16;
  }
  auto bytes = RandomBytes(length);
  return BytesToHex(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

std::string Sha256(const std::string& input) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_len = 0;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) {
    throw std::runtime_error("Unable to allocate EVP_MD_CTX");
  }

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, input.data(), input.size()) != 1 ||
      EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("SHA256 hashing failed");
  }

  EVP_MD_CTX_free(ctx);
  return BytesToHex(digest.data(), digest_len);
}

std::string HashPassword(const std::string& password, const std::string& salt) {
  return Sha256(salt + password);
}

std::string GenerateToken(std::size_t byte_length) {
  if (byte_length == 0) {
    byte_length = 32;
  }
  auto bytes = RandomBytes(byte_length);
  return BytesToHex(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

}  // namespace crypto_utils
