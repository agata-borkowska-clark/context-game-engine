#pragma once

#include "net.h"
#include "result.h"
#include "status.h"

namespace util {

enum class http_status : int {
  ok = 200,
  bad_request = 400,
  payload_too_large = 413,
  request_header_fields_too_large = 431,
  not_implemented = 501,
};

status make_status(http_status) noexcept;
status make_status(http_status, std::string) noexcept;

enum class http_method {
  get,
  post,
};

std::ostream& operator<<(std::ostream&, http_method) noexcept;

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
