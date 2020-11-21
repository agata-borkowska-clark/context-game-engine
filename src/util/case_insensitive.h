#pragma once

#include <string>
#include <string_view>

namespace util {

struct case_insensitive_char_traits : public std::char_traits<char> {
  static bool eq(char, char);
  static bool lt(char, char);
  static int compare(const char*, const char*, std::size_t n);
  static const char* find(const char*, std::size_t, char);
};

using case_insensitive_string =
    std::basic_string<char, case_insensitive_char_traits>;
using case_insensitive_string_view =
    std::basic_string_view<char, case_insensitive_char_traits>;

std::ostream& operator<<(std::ostream&,
                         const case_insensitive_string&) noexcept;

std::ostream& operator<<(std::ostream&, case_insensitive_string_view) noexcept;

}  // namespace util
