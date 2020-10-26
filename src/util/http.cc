#include "http.h"

namespace util {
namespace {

constexpr char canned_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, World!";

struct connection {
  static void spawn(stream client) noexcept {
    auto self = std::make_shared<connection>(std::move(client));
    self->run(self);
  }

  connection(stream client) noexcept : client(std::move(client)) {}

  void run(std::shared_ptr<connection> self) noexcept {
    client.write(canned_response, [self](status s) {
      if (s.failure()) {
        std::cerr << s << '\n';
      }
    });
  }

  stream client;
};

struct accept_handler {
  static void spawn(acceptor& server) noexcept {
    auto self = std::make_shared<accept_handler>(server);
    self->do_accept(self);
  }

  accept_handler(acceptor& server) noexcept : server(server) {}

  void do_accept(std::shared_ptr<accept_handler> self) noexcept {
    server.accept([self](result<stream> client) {
      if (client.failure()) {
        std::cerr << client.status() << '\n';
      } else {
        connection::spawn(std::move(*client));
        self->do_accept(self);
      }
    });
  }

  acceptor& server;
};

}  // namespace

result<http_server> http_server::create(const address& address) noexcept {
  http_server server;
  if (status s = server.init(address); s.failure()) return error{std::move(s)};
  return server;
}

http_server::http_server() noexcept {}

status http_server::init(const address& address) noexcept {
  if (status s = context_.init(); s.failure()) return error{std::move(s)};
  result<acceptor> acceptor = bind(context_, address);
  if (acceptor.failure()) return error{std::move(acceptor).status()};
  acceptor_ = std::move(*acceptor);
  return status_code::ok;
}

status http_server::run() {
  accept_handler::spawn(acceptor_);
  return context_.run();
}

}  // namespace util
