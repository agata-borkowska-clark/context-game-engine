#pragma once

#include "http.h"
#include "net.h"

#include <functional>
#include <iosfwd>
#include <memory>

namespace util {

class websocket {
 public:
  enum class frame_type {
    continuation = 0,
    text = 1,
    binary = 2,
    close = 8,
    ping = 9,
    pong = 10,
  };

  struct message {
    frame_type type;
    span<const char> payload;
  };

  websocket(tcp::stream& socket) noexcept;

  future<result<message>> receive_message(span<char> buffer) noexcept;
  future<status> send_message(message) noexcept;

 private:
  tcp::stream* socket_;
};

std::ostream& operator<<(std::ostream&, websocket::frame_type) noexcept;

class handle_websocket {
 public:
  handle_websocket(std::function<future<status>(websocket&)> handler) noexcept;
  std::unique_ptr<http_handler> operator()(http_method, uri) noexcept;

 private:
  const std::function<future<status>(websocket&)> handler_;
};

}  // namespace util
