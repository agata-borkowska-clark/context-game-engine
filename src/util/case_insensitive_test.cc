#include "case_insensitive.h"

#include "span.h"

#include <iomanip>
#include <iostream>
#include <random>

namespace {

using util::operator""_cis;
using util::operator""_cisv;

struct list { util::span<const util::case_insensitive_string_view> values; };

std::ostream& operator<<(std::ostream& output, const list& l) {
  output << "{";
  bool first = true;
  for (const auto& x : l.values) {
    if (first) {
      first = false;
    } else {
      output << ", ";
    }
    output << std::quoted(x);
  }
  return output << "}";
}

bool operator==(const list& l, const list& r) noexcept {
  return l.values.size() == r.values.size() &&
         std::equal(l.values.begin(), l.values.end(), r.values.begin(),
                    r.values.end());
}

const list& quoted(const list& l) noexcept { return l; }

template <typename L, typename R>
bool expect_eq(L&& l, R&& r) noexcept {
  const bool result = l == r;
  std::cout << quoted(l) << " == " << quoted(r) << ": "
            << (result ? "\x1b[32mPASSED\x1b[0m" : "\x1b[31mFAILED\x1b[0m")
            << '\n';
  return result;
}

}  // namespace

int main() {
  bool success = true;
  success &= expect_eq("Case insensitive"_cis, "case Insensitive"_cis);
  success &= expect_eq("Hello, world!"_cisv, "hello, World!"_cisv);
  util::case_insensitive_string_view inputs[] = {
      "0", "Charlie", "Bob", "Alison", "alice",
      "^", "charlie", "bOb", "aLiSoN", "AlicE",
  };
  // Randomly shuffle the input.
  std::random_device seed_source;
  std::mt19937 random(seed_source());
  std::shuffle(std::begin(inputs), std::end(inputs), random);
  // Sort the input and remove duplicates.
  std::sort(std::begin(inputs), std::end(inputs));
  const auto end = std::unique(std::begin(inputs), std::end(inputs));
  const list actual = {util::span<const util::case_insensitive_string_view>(
      inputs, end - inputs)};
  // Expect the output to have case insensitive duplicates removed and be sorted
  // as if the text was all uppercase.
  const util::case_insensitive_string_view expected[] = {
      "0", "ALICE", "ALISON", "BOB", "CHARLIE", "^",
  };
  success &= expect_eq(list{expected}, actual);
  return success ? 0 : 1;
}
