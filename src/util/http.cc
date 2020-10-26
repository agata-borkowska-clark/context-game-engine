#include "http.h"

#include "status_managers.h"

namespace util {
namespace {

struct http_status_manager_base : status_manager {
  constexpr std::uint64_t domain_id() const noexcept final {
    return 0x36'59'86'42'5c'5a'8b'53;
  }
  constexpr std::string_view domain() const noexcept final {
    return "http_status";
  }
  std::string_view name(status_payload payload) const noexcept final {
    switch (http_status{code(payload)}) {
      case http_status::ok: return "ok";
    }
    return "<invalid>";
  }
  bool failure(status_payload payload) const noexcept final {
    const int kind = code(payload) / 100 * 100;
    // 1xx is informational (not a failure), 2xx is success, 3xx is redirect.
    return kind == 100 || kind == 200 || kind == 300;
  }
  status_code canonical(status_payload payload) const noexcept final {
    switch (code(payload) / 100 * 100) {
      case 100: return status_code::ok;
      case 200: return status_code::ok;
      case 300: return status_code::ok;
      case 400: return status_code::client_error;
      case 500: return status_code::transient_error;
      default: return status_code::unknown_error;
    }
  }
};

// Status managers for http status codes.
static constexpr code_manager<http_status_manager_base> http_status_manager;
static constexpr code_with_message_manager<http_status_manager_base>
    http_status_with_message_manager;

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

status make_status(http_status code) noexcept {
  return http_status_manager.make(code);
}

status make_status(http_status code, std::string message) noexcept {
  return http_status_with_message_manager.make(code, std::move(message));
}

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
