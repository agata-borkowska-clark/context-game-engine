#pragma once

#include "net.h"
#include "result.h"
#include "status.h"

#include <map>

namespace util {

enum class http_status : int {
  ok = 200,
  bad_request = 400,
  not_found = 404,
  payload_too_large = 413,
  request_header_fields_too_large = 431,
  internal_server_error = 500,
  not_implemented = 501,
};

status make_status(http_status) noexcept;
status make_status(http_status, std::string) noexcept;

enum class http_method {
  get,
  post,
};

std::ostream& operator<<(std::ostream&, http_method) noexcept;

struct uri {
  // For an example URI `http://www.example.com:42/demo?q=42#f`:
  std::string scheme;     // e.g. `http`
  std::string authority;  // e.g. `www.example.com`
  std::string path;       // e.g. `/demo`
  std::string query;      // e.g. `q=42`
  std::string fragment;   // e.g. `f`
};

result<uri> parse_uri(std::string_view input) noexcept;

struct http_response {
  std::string_view payload;
  std::string content_type;
};

struct http_request {
  http_method method;
  uri target;
  std::string_view payload;
  std::function<void(result<http_response>)> respond;
};

class http_server {
 public:
  using handler = std::function<void(http_request)>;

  // Equivalent to constructing a http_server and calling init().
  result<http_server> create(io_context&, const address&) noexcept;

  // Construct an uninitialised http server.
  http_server(io_context& context) noexcept;

  // Initialise the http server by binding it to the given address.
  status init(const address&) noexcept;

  // Add a handler for the given path.
  void handle(std::string path, handler) noexcept;

  // Handle work for the server.
  void start() noexcept;

 private:
  io_context* context_;
  tcp::acceptor acceptor_;
  std::map<std::string, handler> handlers_;
};

}  // namespace util
