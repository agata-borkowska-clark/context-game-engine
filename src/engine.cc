#include "util/status.h"

#include <iostream>
#include <random>

util::status risky() {
  std::random_device device;
  std::mt19937 generator(device());
  bool success = std::bernoulli_distribution(0.9)(generator);
  if (success) {
    return util::status_code::ok;
  } else {
    return util::internal_error("a bad did a happening :(");
  }
}

int main() {
  if (util::status status = risky(); status.success()) {
    std::cout << "You win!\n";
  } else {
    std::cout << "Oh no, you lose! " << status << '\n';
    return 1;
  }
}
