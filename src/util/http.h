#pragma once

#include "net.h"
#include "result.h"
#include "status.h"

namespace util {

enum http_status : int {
  ok = 200,
};

status make_status(http_status) noexcept;
status make_status(http_status, std::string) noexcept;

class http_server {
 public:
  // Equivalent to constructing a http_server and calling init().
  result<http_server> create(const address&) noexcept;

  // Construct an uninitialised http server.
  http_server() noexcept;

  // Initialise the http server by binding it to the given address.
  status init(const address&) noexcept;

  // Handle work for the server.
  status run();

 private:
  io_context context_;
  acceptor acceptor_;
};

}  // namespace util
