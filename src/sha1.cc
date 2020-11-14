#include "util/sha1.h"

#include <iostream>
#include <string>
#include <string_view>

char buffer[1 << 20];

int main(int argc, char* argv[]) {
  std::string input{std::istreambuf_iterator<char>(std::cin), {}};
  std::cout << util::sha1(input) << '\n';
}
