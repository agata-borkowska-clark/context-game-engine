#include "util/result.h"

#include <iostream>
#include <random>

util::result<int> risky() {
  std::random_device device;
  std::mt19937 generator(device());
  bool success = std::bernoulli_distribution(0.9)(generator);
  if (success) {
    return 42;
  } else {
    return util::internal_error("a bad did a happening :(");
  }
}

int main() {
  if (util::result<int> result = risky(); result.success()) {
    std::cout << "You win with " << *result << " points!\n";
  } else {
    std::cout << "Oh no, you lose! " << result.status() << '\n';
    return 1;
  }
}
