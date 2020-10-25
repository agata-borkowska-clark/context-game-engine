#include "serial.h"

namespace util {

void encoder::encode(unsigned int t) {
  static_assert(sizeof(t) == 4);
  out_stream << static_cast<unsigned char>(t >> 24);
  out_stream << static_cast<unsigned char>(t >> 16);
  out_stream << static_cast<unsigned char>(t >> 8);
  out_stream << static_cast<unsigned char>(t);
}

void encoder::encode(int t) {
  unsigned int t2 = static_cast<unsigned int>(t);
  encoder::encode(t2);
}

void encoder::encode(char t) {
  out_stream << t;
}

}  // namespace util

