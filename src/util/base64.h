#pragma once

#include "result.h"
#include "span.h"

#include <string_view>

namespace util {

// Given a size n representing the size in bytes of a payload, returns the
// number of bytes required for the base64 representation of that payload.
constexpr std::size_t base64_encoded_size(std::size_t n) noexcept {
  return (n + 2) / 3 * 4;
}

// Given a size n representing the size in bytes of a base64 string, returns the
// maximum number of bytes that will be required for the decoded payload.
constexpr std::size_t base64_decoded_size(std::size_t n) noexcept {
  return (n + 3) / 4 * 3;
}

// Given a span of binary data and a buffer to write into, encode the data in
// base64 and return a span to the prefix of the buffer which was used.
std::string_view base64_encode(std::string_view data,
                               span<char> buffer) noexcept;

// Given a span containing a base64 string and a buffer to write into, decode
// the data and return a span to the prefix of the buffer which contains the
// result, or an error indicating why this was not possible.
result<std::string_view> base64_decode(std::string_view data,
                                       span<char> buffer) noexcept;

}  // namespace util
