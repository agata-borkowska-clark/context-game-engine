#include "util/result.h"
#include "util/executor.h"

#include <chrono>
#include <iostream>
#include <random>

using std::chrono_literals::operator""ms;

// risky() is a function that fails frequently and otherwise returns a score.
util::result<int> risky() {
  std::random_device device;
  std::mt19937 generator(device());
  bool success = std::bernoulli_distribution(0.1)(generator);
  if (success) {
    return std::uniform_int_distribution(1, 100)(generator);
  } else {
    return util::internal_error("a bad did a happening :(");
  }
}

// nth{x} pretty-prints an integer with a suffix.
enum class nth : int {};
std::ostream& operator<<(std::ostream& output, nth n) {
  switch ((int)n) {
    case 1: return output << (int)n << "st";
    case 2: return output << (int)n << "nd";
    case 3: return output << (int)n << "rd";
    default: return output << (int)n << "th";
  }
}

// Repeatedly try the risky operation until it succeeds, using the given
// executor to perform the attempts and keeping track of the number of attempts
// in the given int.
void make_attempt(util::executor& executor, int& attempts) {
  attempts++;
  if (auto result = risky(); result.success()) {
    std::cout << "You win on the " << nth{attempts} << " attempt with "
              << *result << " points!\n";
  } else {
    std::cout << "Blast, failed! Trying again...\n";
    executor.schedule_in(100ms, [&] { make_attempt(executor, attempts); });
  }
}

int main() {
  int attempts = 0;
  util::serial_executor executor;
  make_attempt(executor, attempts);
  executor.run();
}
