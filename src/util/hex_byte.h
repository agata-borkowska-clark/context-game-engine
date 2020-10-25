#include <iostream>

namespace util {

enum class hex_byte : unsigned char {};
inline std::ostream& operator<<(std::ostream& output, hex_byte h) {
  constexpr char kHex[] = "0123456789abcdef";
  return output << kHex[(unsigned char)h >> 4] << kHex[(unsigned char)h & 0xF];
}
// TODO write a utility for converting a stream of bytes

}  // namespace util

