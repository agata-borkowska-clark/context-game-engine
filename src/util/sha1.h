#pragma once

#include "span.h"

#include <iosfwd>

namespace util {

struct sha1 {
  sha1(span<const char> input) noexcept;
  char bytes[20];
};

std::ostream& operator<<(std::ostream&, const sha1&) noexcept;

}  // namespace util
