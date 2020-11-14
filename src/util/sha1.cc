#include "sha1.h"

#include <cstdint>
#include <iostream>

namespace util {
namespace {

template <int count>
std::uint32_t left_rotate(std::uint32_t value) noexcept {
  static_assert(count != 0 && count < 32);
  return value << count | value >> (32 - count);
}

struct state {
  void hash_block(const char* block) noexcept {
    std::uint32_t w[80];
    // Read 16 big-endian 32-bit words from the input.
    for (int i = 0; i < 16; i++) {
      // Widen a char to an unsigned int without sign extension.
      constexpr auto f = [](char c) { return (unsigned int)(unsigned char)c; };
      const char* word = block + 4 * i;
      w[i] = f(word[0]) << 24 | f(word[1]) << 16 | f(word[2]) << 8 | f(word[3]);
    }
    // Widen this to 80 32-bit words with some xors and rotates.
    for (int i = 16; i < 80; i++) {
      w[i] = left_rotate<1>(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]);
    }
    // Hash the chunk.
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    auto step = [&](std::uint32_t f, std::uint32_t k, std::uint32_t w) {
      std::uint32_t temp = left_rotate<5>(a) + f + e + k + w;
      e = d;
      d = c;
      c = left_rotate<30>(b);
      b = a;
      a = temp;
    };
    // Main hash loops.
    // The four magic constants are derived from floor(2^30 * sqrt(x)) for each
    // of 2, 3, 5, and 10.
    for (int i = 0; i < 20; i++) {
      step((b & c) | (~b & d), 0x5A827999, w[i]);
    }
    for (int i = 20; i < 40; i++) {
      step(b ^ c ^ d, 0x6ED9EBA1, w[i]);
    }
    for (int i = 40; i < 60; i++) {
      step((b & c) | (b & d) | (c & d), 0x8F1BBCDC, w[i]);
    }
    for (int i = 60; i < 80; i++) {
      step(b ^ c ^ d, 0xCA62C1D6, w[i]);
    }
    // Update the state.
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  // These magic constants are fairly obvious bit patterns.
  std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                        0xC3D2E1F0};
};

}  // namespace

sha1::sha1(span<const char> input) noexcept {
  const int n = input.size();
  state state;
  for (int block_end = 64; block_end <= n; block_end += 64) {
    state.hash_block(input.data() + block_end - 64);
  }
  // Finish by appending 0x80 followed by enough 0 bytes to suitably pad a
  // block, followed by the original message length as a big-endian 64-bit int.
  const int tail_size = n % 64;
  const int tail_start = n - tail_size;
  assert(tail_start % 64 == 0);
  assert(tail_size < 64);
  char block[64] = {};
  for (int i = 0; i < tail_size; i++) block[i] = input[tail_start + i];
  block[tail_size] = (unsigned char)0x80;
  if (tail_size < 56) {
    const std::uint64_t ml = (std::uint64_t)n * 8;
    block[64 - 8] = (unsigned char)(ml >> 56);
    block[64 - 7] = (unsigned char)(ml >> 48);
    block[64 - 6] = (unsigned char)(ml >> 40);
    block[64 - 5] = (unsigned char)(ml >> 32);
    block[64 - 4] = (unsigned char)(ml >> 24);
    block[64 - 3] = (unsigned char)(ml >> 16);
    block[64 - 2] = (unsigned char)(ml >> 8);
    block[64 - 1] = (unsigned char)(ml);
    state.hash_block(block);
  } else {
    state.hash_block(block);
    char final_block[64] = {};
    const std::uint64_t ml = (std::uint64_t)n * 8;
    final_block[64 - 8] = (unsigned char)(ml >> 56);
    final_block[64 - 7] = (unsigned char)(ml >> 48);
    final_block[64 - 6] = (unsigned char)(ml >> 40);
    final_block[64 - 5] = (unsigned char)(ml >> 32);
    final_block[64 - 4] = (unsigned char)(ml >> 24);
    final_block[64 - 3] = (unsigned char)(ml >> 16);
    final_block[64 - 2] = (unsigned char)(ml >> 8);
    final_block[64 - 1] = (unsigned char)(ml);
    state.hash_block(final_block);
  }
  for (int i = 0; i < 5; i++) {
    char* word = bytes + 4 * i;
    word[0] = (unsigned char)(state.h[i] >> 24);
    word[1] = (unsigned char)(state.h[i] >> 16);
    word[2] = (unsigned char)(state.h[i] >> 8);
    word[3] = (unsigned char)(state.h[i]);
  }
}

std::ostream& operator<<(std::ostream& output, const sha1& hash) noexcept {
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char byte : hash.bytes) {
    output << hex[byte >> 4] << hex[byte & 0xF];
  }
  return output;
}

}  // namespace util
