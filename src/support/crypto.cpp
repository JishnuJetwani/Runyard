#include "runyard/support/crypto.hpp"
#include <array>
#include <fstream>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace runyard {
namespace {
std::string hex(const unsigned char *bytes, std::size_t length) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result(length * 2, '0');
  for (std::size_t i = 0; i < length; ++i) {
    result[i * 2] = digits[bytes[i] >> 4];
    result[i * 2 + 1] = digits[bytes[i] & 15];
  }
  return result;
}
using Digest = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
Digest digest() {
  Digest ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("digest initialization failed");
  return ctx;
}
std::string finish(EVP_MD_CTX *ctx) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned length = 0;
  if (EVP_DigestFinal_ex(ctx, bytes.data(), &length) != 1)
    throw std::runtime_error("digest failed");
  return hex(bytes.data(), length);
}
} // namespace
std::string random_id() {
  std::array<unsigned char, 16> data{};
  if (RAND_bytes(data.data(), data.size()) != 1)
    throw std::runtime_error("random generation failed");
  return hex(data.data(), data.size());
}
std::string sha256(std::string_view data) {
  auto ctx = digest();
  EVP_DigestUpdate(ctx.get(), data.data(), data.size());
  return finish(ctx.get());
}
std::string sha256_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open file for checksum");
  auto ctx = digest();
  std::array<char, 65536> buffer{};
  while (input) {
    input.read(buffer.data(), buffer.size());
    EVP_DigestUpdate(ctx.get(), buffer.data(), input.gcount());
  }
  if (!input.eof())
    throw std::runtime_error("checksum read failed");
  return finish(ctx.get());
}
std::string sign(std::string_view secret, std::string_view message) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> data{};
  unsigned length{};
  if (!HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
            reinterpret_cast<const unsigned char *>(message.data()), message.size(), data.data(),
            &length))
    throw std::runtime_error("signing failed");
  return hex(data.data(), length);
}
bool constant_equal(std::string_view left, std::string_view right) {
  return left.size() == right.size() && CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}
} // namespace runyard
