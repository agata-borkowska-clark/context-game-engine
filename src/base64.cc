#include "util/base64.h"

#include <iostream>
#include <string>
#include <string_view>

char buffer[1 << 20];

int main(int argc, char* argv[]) {
  std::string input{std::istreambuf_iterator<char>(std::cin), {}};
  if (util::base64_encoded_size(input.size()) > sizeof(buffer)) {
    std::cerr << "Too big.\n";
    return 1;
  }
  util::span<char> encoded = util::base64_encode(input, buffer);
  std::cout.write(encoded.data(), encoded.size());
  std::cout << '\n';
  util::result<util::span<char>> decoded = util::base64_decode(encoded, buffer);
  if (decoded.failure()) {
    std::cerr << decoded.status() << '\n';
    return 1;
  }
  std::cout.write(decoded->data(), decoded->size());
}
