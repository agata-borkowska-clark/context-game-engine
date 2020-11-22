#include "base64.h"

#include <array>

namespace util {
namespace {

constexpr auto tables = [] {
  struct {
    std::array<char, 64> encode;
    std::array<unsigned char, 256> decode;
  } tables = {};
  auto set = [&](char b, unsigned char value) {
    tables.encode[value] = b;
    tables.decode[(unsigned char)b] = value;
  };
  for (auto& x : tables.decode) x = 0xFF;
  for (int i = 0; i < 26; i++) set('A' + i, i);
  for (int i = 0; i < 26; i++) set('a' + i, 26 + i);
  for (int i = 0; i < 10; i++) set('0' + i, 52 + i);
  set('+', 62);
  set('/', 63);
  return tables;
}();

}  // namespace

std::string_view base64_encode(std::string_view data,
                               span<char> buffer) noexcept {
  const int n = data.size();
  // The programmer can always ensure that they provide a big enough buffer
  // here, so this case is not exposed via an error code.
  assert(base64_encoded_size(n) <= buffer.size());
  int i = 0;
  int o = 0;
  while (i + 3 <= n) {
    const unsigned char a = data[i], b = data[i + 1], c = data[i + 2];
    buffer[o + 0] = tables.encode[0x3F & (a >> 2)];
    buffer[o + 1] = tables.encode[0x3F & (a << 4 | b >> 4)];
    buffer[o + 2] = tables.encode[0x3F & (b << 2 | c >> 6)];
    buffer[o + 3] = tables.encode[0x3F & c];
    i += 3, o += 4;
  }
  switch (n - i) {
    case 0:
      break;
    case 1: {
      const unsigned char a = data[i];
      buffer[o + 0] = tables.encode[0x3F & (a >> 2)];
      buffer[o + 1] = tables.encode[0x3F & (a << 4)];
      buffer[o + 2] = '=';
      buffer[o + 3] = '=';
      o += 4;
      break;
    }
    case 2: {
      const unsigned char a = data[i], b = data[i + 1];
      // AAAAAA AABBBB BBBBCC CCCCCC
      buffer[o + 0] = tables.encode[0x3F & (a >> 2)];
      buffer[o + 1] = tables.encode[0x3F & (a << 4 | b >> 4)];
      buffer[o + 2] = tables.encode[0x3F & (b << 2)];
      buffer[o + 3] = '=';
      o += 4;
    }
  }
  assert(base64_encoded_size(n) == o);
  return std::string_view(buffer.data(), o);
}

result<std::string_view> base64_decode(std::string_view data,
                                       span<char> buffer) noexcept {
  const int n = data.size();
  // All base64 strings are a multiple of 4 bytes in length, with padding.
  if (data.size() % 4 != 0) return error{status_code::client_error};
  if (data.empty()) return "";
  // The programmer can always ensure that they provide a big enough buffer
  // here, so this case is not exposed via an error code.
  assert(base64_decoded_size(n) <= buffer.size());
  int i = 0;
  int o = 0;
  const int padding = data[n - 1] == '=' ? data[n - 2] == '=' ? 2 : 1 : 0;
  const int last_full = padding ? n - 4 : n;
  while (i + 4 <= last_full) {
    const unsigned char a = tables.decode[data[i]],
                        b = tables.decode[data[i + 1]],
                        c = tables.decode[data[i + 2]],
                        d = tables.decode[data[i + 3]];
    if (a == 0xFF || b == 0xFF || c == 0xFF || d == 0xFF) {
      return error{status_code::client_error};
    }
    // AAAAAABB BBBBCCCC CCDDDDDD
    buffer[o + 0] = (unsigned char)(a << 2 | b >> 4);
    buffer[o + 1] = (unsigned char)(b << 4 | c >> 2);
    buffer[o + 2] = (unsigned char)(c << 6 | d);
    i += 4, o += 3;
  }
  switch (padding) {
    case 0:
      break;
    case 1: {
      const unsigned char a = tables.decode[data[i]],
                          b = tables.decode[data[i + 1]],
                          c = tables.decode[data[i + 2]];
      if (a == 0xFF || b == 0xFF || c == 0xFF) {
        return error{status_code::client_error};
      }
      // AAAAAABB BBBBCCCC | CC <- discarded
      buffer[o + 0] = (unsigned char)(a << 2 | b >> 4);
      buffer[o + 1] = (unsigned char)(b << 4 | c >> 2);
      o += 2;
      break;
    }
    case 2: {
      const unsigned char a = tables.decode[data[i]],
                          b = tables.decode[data[i + 1]];
      if (a == 0xFF || b == 0xFF) {
        return error{status_code::client_error};
      }
      // AAAAAABB | BBBB <- discarded
      buffer[o + 0] = (unsigned char)(a << 2 | b >> 4);
      o += 1;
      break;
    }
  }
  return std::string_view(buffer.data(), o);
}

}  // namespace util
