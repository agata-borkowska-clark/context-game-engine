#include "base64.h"

#include <iomanip>
#include <string_view>

namespace {

struct test_case {
  std::string_view input;
  std::string_view expected_output;
};

constexpr test_case test_cases[] = {
  {.input = "length % 3 == 0", .expected_output = "bGVuZ3RoICUgMyA9PSAw"},
  {.input = "length % 3 == +1", .expected_output = "bGVuZ3RoICUgMyA9PSArMQ=="},
  {.input = "length % 3 == two", .expected_output = "bGVuZ3RoICUgMyA9PSB0d28="},
  {.input = "Hello, World!", .expected_output = "SGVsbG8sIFdvcmxkIQ=="},
};

}  // namespace

int main() {
  bool success = true;
  for (const auto [input, expected_output] : test_cases) {
    std::cout << "base64(" << std::quoted(input) << "): ";
    char buffer[1024];
    const std::string_view output = util::base64_encode(input, buffer);
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
