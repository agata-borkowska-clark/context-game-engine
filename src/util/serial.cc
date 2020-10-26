#include "serial.h"

namespace util {

void encoder<unsigned int>::operator() (const unsigned int& value) const noexcept {
  static_assert(sizeof(value) == 4);
  *out_stream_ << (value >> 24);
  *out_stream_ << (value >> 16);
  *out_stream_ << (value >> 8);
  *out_stream_ << (value);
}

/*encoder<float> operator() (char& value) const noexcept {
  // TODO
}*/

void encoder<char>::operator() (const char& value) const noexcept {
  *out_stream_ << value;
}

}  // namespace util

