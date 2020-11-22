#include "sha1.h"

#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

struct test_case {
  std::string_view input;
  std::string_view expected_output;
};

constexpr test_case test_cases[] = {
    {.input = "",
     .expected_output = "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {.input = "Hello, World!",
     .expected_output = "0a0a9f2a6772942557ab5355d76af442f8f65e01"},
    {.input = "A string which is between fifty-five and sixty-four bytes",
     .expected_output = "c0376cb1a88a00cd1bdcb08b7dbad4c6d80387b6"},
    {.input = "A string exceeding sixty-four bytes, requiring multiple blocks",
     .expected_output = "bc796def7aef8a9a2b4a49bb43bf6e14a73c0936"},
};

}  // namespace

int main() {
  bool success = true;
  for (const auto [input, expected_output] : test_cases) {
    std::cout << "sha1(" << std::quoted(input) << "): ";
    char buffer[1024];
    std::ostringstream stream;
    stream << util::sha1(input);
    const std::string output = stream.str();
    if (output == expected_output) {
      std::cout << "\x1b[32mPASSED\x1b[0m\n";
    } else {
      std::cout << "\x1b[31mFAILED\x1b[0m\n"
                << "got:  " << std::quoted(output) << '\n'
                << "want: " << std::quoted(expected_output) << '\n';
      success = false;
    }
  }
  return success ? 0 : 1;
}
