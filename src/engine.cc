#include "util/executor.h"
#include "util/net.h"
#include "util/result.h"

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>

using std::chrono_literals::operator""ms;

// risky() is a function that fails frequently and otherwise returns a score.
util::result<int> risky() {
  std::random_device device;
  std::mt19937 generator(device());
  bool success = std::bernoulli_distribution(0.1)(generator);
  if (success) {
    return std::uniform_int_distribution(1, 100)(generator);
  } else {
    return util::transient_error("a bad did a happening :(");
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

struct connection {
  static void spawn(util::stream client) {
    auto c = std::make_shared<connection>(std::move(client));
    c->make_attempt(c);
  }

  connection(util::stream client) : client(std::move(client)) {}

  void make_attempt(std::shared_ptr<connection> self) {
    attempts++;
    if (auto result = risky(); result.success()) {
      std::ostringstream output;
      output << "You win on the " << nth{attempts} << " attempt with "
             << *result << " points!\n";
      message = output.str();
      client.write(message, [self](util::status s) {
        if (s.failure()) {
          std::cerr << s << '\n';
        } else {
          self->done(self);
        }
      });
    } else {
      std::ostringstream output;
      output << "Blast, failed the " << nth{attempts}
             << " attempt! Trying again...\n";
      message = output.str();
      client.write(message, [self](util::status s) {
        if (s.failure()) {
          std::cerr << s << '\n';
        } else {
          self->client.context().schedule_in(
              100ms, [self] { self->make_attempt(self); });
        }
      });
    }
  }

  void done(std::shared_ptr<connection>) {
    std::cout << "Done!\n";
  }

  util::stream client;
  std::string message;
  int attempts = 0;
};

struct server {
  static void spawn(util::io_context& context) {
    util::result<util::acceptor> acceptor =
        util::bind(context, {"0.0.0.0", 17994});
    if (acceptor.failure()) {
      std::cerr << acceptor.status() << '\n';
      return;
    }
    auto s = std::make_shared<server>(std::move(*acceptor));
    s->do_accept(s);
  }
  server(util::acceptor acceptor) : acceptor(std::move(acceptor)) {}
  void do_accept(std::shared_ptr<server> self) {
    acceptor.accept([self](util::result<util::stream> client) {
      if (client.failure()) {
        std::cerr << client.status() << '\n';
      } else {
        connection::spawn(std::move(*client));
        self->do_accept(self);
      }
    });
  }

  util::acceptor acceptor;
};

int main() {
  util::result<util::io_context> context = util::io_context::create();
  if (context.failure()) {
    std::cerr << context.status() << '\n';
    return 1;
  }
  server::spawn(*context);
  if (util::status s = context->run(); s.failure()) {
    std::cerr << s << '\n';
  }
}
