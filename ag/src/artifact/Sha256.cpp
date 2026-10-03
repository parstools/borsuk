#include "agas/artifact/Sha256.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace agas::artifact {
namespace {

constexpr std::array<std::uint32_t, 64> constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

void processBlock(std::array<std::uint32_t, 8> &state,
                  const unsigned char *block) {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    const std::size_t offset = index * 4;
    words[index] = static_cast<std::uint32_t>(block[offset]) << 24U |
                   static_cast<std::uint32_t>(block[offset + 1]) << 16U |
                   static_cast<std::uint32_t>(block[offset + 2]) << 8U |
                   static_cast<std::uint32_t>(block[offset + 3]);
  }
  for (std::size_t index = 16; index < words.size(); ++index) {
    const std::uint32_t first = std::rotr(words[index - 15], 7) ^
                                std::rotr(words[index - 15], 18) ^
                                (words[index - 15] >> 3U);
    const std::uint32_t second = std::rotr(words[index - 2], 17) ^
                                 std::rotr(words[index - 2], 19) ^
                                 (words[index - 2] >> 10U);
    words[index] = words[index - 16] + first + words[index - 7] + second;
  }

  auto [a, b, c, d, e, f, g, h] = state;
  for (std::size_t index = 0; index < words.size(); ++index) {
    const std::uint32_t sigma1 =
        std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const std::uint32_t choice = (e & f) ^ (~e & g);
    const std::uint32_t first =
        h + sigma1 + choice + constants[index] + words[index];
    const std::uint32_t sigma0 =
        std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t second = sigma0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

} // namespace

auto sha256Hex(std::string_view bytes) -> std::string {
  if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8U)
    throw std::length_error("SHA-256 input is too large");
  std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                     0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                     0x1f83d9abU, 0x5be0cd19U};
  const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
  std::size_t offset = 0;
  while (bytes.size() - offset >= 64) {
    processBlock(state, data + offset);
    offset += 64;
  }

  std::array<unsigned char, 128> tail{};
  const std::size_t remaining = bytes.size() - offset;
  if (remaining != 0)
    std::memcpy(tail.data(), data + offset, remaining);
  tail[remaining] = 0x80U;
  const std::size_t paddedBytes = remaining < 56 ? 64 : 128;
  const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8U;
  for (std::size_t index = 0; index < 8; ++index)
    tail[paddedBytes - 1 - index] =
        static_cast<unsigned char>(bitLength >> (index * 8U));
  processBlock(state, tail.data());
  if (paddedBytes == 128)
    processBlock(state, tail.data() + 64);

  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const std::uint32_t value : state)
    for (int shift = 28; shift >= 0; shift -= 4)
      result.push_back(hex[(value >> shift) & 0xFU]);
  return result;
}

} // namespace agas::artifact
