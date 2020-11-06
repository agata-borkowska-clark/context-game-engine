#pragma once

#include <string_view>

namespace util {

// Returns a string view of the contents of the given file, which is assumed to
// be read-only for the entire server lifetime. The returned string_view remains
// valid for the whole lifetime of the program. If the file cannot be opened,
// the program will exit.
std::string_view contents(const char* filename) noexcept;

}  // namespace util
