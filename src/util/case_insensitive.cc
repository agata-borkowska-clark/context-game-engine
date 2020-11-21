#include "case_insensitive.h"

#include <iostream>

namespace util {
namespace {

char to_upper(char c) { return std::toupper((unsigned char)c); }

}  // namespace

bool case_insensitive_char_traits::eq(char c1, char c2) {
  return to_upper(c1) == to_upper(c2);
}

bool case_insensitive_char_traits::lt(char c1, char c2) {
  return to_upper(c1) < to_upper(c2);
}

int case_insensitive_char_traits::compare(const char* s1, const char* s2,
                                          std::size_t n) {
  while (n--) {
    const char c1 = to_upper(*s1++), c2 = to_upper(*s2++);
    if (c1 < c2) return -1;
    if (c1 > c2) return 1;
  }
  return 0;
}

const char* case_insensitive_char_traits::find(const char* s, std::size_t n,
                                               char a) {
  const char c = to_upper(a);
  while (n--) {
    if (to_upper(*s) == c) return s;
    s++;
  }
  return nullptr;
}

std::ostream& operator<<(std::ostream& output,
                         const case_insensitive_string& s) noexcept {
  return output.write(s.data(), s.size());
}

std::ostream& operator<<(std::ostream& output,
                         case_insensitive_string_view s) noexcept {
  return output.write(s.data(), s.size());
}

}  // namespace util
