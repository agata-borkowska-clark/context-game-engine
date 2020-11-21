#include "http.h"

#include "case_insensitive.h"
#include "future.h"
#include "status_managers.h"

#include <charconv>
#include <regex>
#include <string_view>

namespace util {
namespace {

using handler_map = std::map<std::string, http_server::handler>;

constexpr bool is_whitespace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r';
}

std::string_view trim(std::string_view value) noexcept {
  const char* i = value.data();
  const char* j = i + value.size();
  while (i != j && is_whitespace(*i)) ++i;
  while (j != i && is_whitespace(j[-1])) --j;
  return std::string_view(i, j - i);
}

result<int> parse_int(std::string_view value) {
  const char* const first = value.data();
  const char* const last = first + value.size();
  int result;
  const auto [ptr, code] = std::from_chars(first, last, result);
  if (ptr != last || code != std::errc{}) {
    return error{status(http_status::bad_request)};
  }
  return result;
}

struct http_status_manager_base : status_manager {
  constexpr std::uint64_t domain_id() const noexcept final {
    return 0x36'59'86'42'5c'5a'8b'53;
  }
  constexpr std::string_view domain() const noexcept final {
    return "http_status";
  }
  std::string_view name(status_payload payload) const noexcept final {
    http_status status{code(payload)};
    switch (status) {
      case http_status::ok:
        return "ok";
      case http_status::bad_request:
        return "bad_request";
      case http_status::not_found:
        return "not_found";
      case http_status::payload_too_large:
        return "payload_too_large";
      case http_status::request_header_fields_too_large:
        return "request_header_fields_too_large";
      case http_status::not_implemented:
        return "not_implemented";
    }
    switch ((int)status / 100 * 100) {
      case 100: return "<informational>";
      case 200: return "<success>";
      case 300: return "<redirect>";
      case 400: return "<client error>";
      case 500: return "<server error>";
    }
    return "<invalid>";
  }
  bool failure(status_payload payload) const noexcept final {
    const int kind = code(payload) / 100 * 100;
    // 1xx is informational (not a failure), 2xx is success, 3xx is redirect.
    return !(kind == 100 || kind == 200 || kind == 300);
  }
  status_code canonical(status_payload payload) const noexcept final {
    switch (code(payload) / 100 * 100) {
      case 100: return status_code::ok;
      case 200: return status_code::ok;
      case 300: return status_code::ok;
      case 400: return status_code::client_error;
      case 500: return status_code::unknown_error;
      default: return status_code::unknown_error;
    }
  }
};

// Status managers for http status codes.
static constexpr code_manager<http_status_manager_base> http_status_manager;
static constexpr code_with_message_manager<http_status_manager_base>
    http_status_with_message_manager;

http_status code(const status& s) noexcept {
  if (s.domain().domain() == "http") return http_status{s.code()};
  switch (status_code{s.canonical().code()}) {
    case status_code::ok:
      return http_status::ok;
    case status_code::client_error:
      return http_status::bad_request;
    case status_code::transient_error:
      return http_status::internal_server_error;
    case status_code::permanent_error:
      return http_status::internal_server_error;
    case status_code::not_available:
      return http_status::bad_request;
    case status_code::unknown_error:
      return http_status::internal_server_error;
    default:
      return http_status::internal_server_error;
  }
}

// Read a single line from a stream. This implementation is hopelessly
// inefficient (it calls read repeatedly for single bytes) but it avoids reading
// too far in the stream and having to keep track of the trailing bytes.
// A better implementation would be to add buffering for the stream so that
// single byte reads do not all result in syscalls.
future<result<std::string_view>> read_line(tcp::stream& client,
                                           span<char> buffer) noexcept {
  int length = 0;
  for (int i = 0, n = buffer.size(); i < n; i++) {
    char temp[1];
    result<span<char>> x = co_await client.read_some(temp);
    if (x.failure()) co_return error{std::move(x).status()};
    if (x->empty()) co_return error{http_status::bad_request};
    char c = (*x)[0];
    if (c == '\n') co_return std::string_view(buffer.data(), length);
    if (c != '\r') buffer[length++] = c;
  }
  co_return error{http_status::request_header_fields_too_large};
}

struct request_line {
  http_method method;
  uri target;
};

// Parse an HTTP method.
result<http_method> parse_method(case_insensitive_string_view method) noexcept {
  if (method == "GET") return http_method::get;
  if (method == "POST") return http_method::post;
  return error{status(http_status::bad_request, "unknown method")};
}

future<result<request_line>> read_request_line(tcp::stream& client) noexcept {
  char buffer[1024];
  result<std::string_view> line = co_await read_line(client, buffer);
  if (line.failure()) co_return error{std::move(line).status()};
  const char* const first = line->data();
  const char* const last = line->data() + line->size();
  const char* const method_end = std::find(first, last, ' ');
  if (method_end == last) {
    co_return error{status(http_status::bad_request, "cannot parse request line")};
  }
  result<http_method> method =
      parse_method(case_insensitive_string_view(first, method_end - first));
  if (method.failure()) co_return error{std::move(method).status()};
  const char* const uri_begin = method_end + 1;
  const char* const uri_end = std::find(uri_begin, last, ' ');
  result<uri> uri = parse_uri(std::string_view(uri_begin, uri_end - uri_begin));
  if (uri.failure()) co_return error{std::move(uri).status()};
  co_return request_line{*method, std::move(*uri)};
}

struct header_pair {
  case_insensitive_string_view name;
  std::string_view value;
};

result<header_pair> parse_header_pair(std::string_view line) noexcept {
  const char* const first = line.data();
  const char* const last = first + line.size();
  const char* const header_name_end = std::find(first, last, ':');
  if (header_name_end == last) {
    return error{
        status(http_status::bad_request, "bad header: " + std::string(line))};
  }
  const case_insensitive_string_view header_name(first,
                                                 header_name_end - first);
  if (header_name.empty()) {
    return error{status(http_status::bad_request, "empty header name")};
  }
  if (is_whitespace(header_name.front()) ||
      is_whitespace(header_name.back())) {
    return error{status(http_status::bad_request, "whitespace in header name")};
  }
  const std::string_view header_value =
      trim(std::string_view(header_name_end + 1, last - header_name_end - 1));
  return header_pair{.name = header_name, .value = header_value};
}

future<status> send_response(tcp::stream& client, http_status s,
                             const http_response& r) noexcept {
  // TODO: Consider finding a better way of serializing the result so that it
  // doesn't need ostreams.
  std::ostringstream header_stream;
  header_stream << "HTTP/1.1 " << (int)s << ' ' << status(s) << "\r\n"
                << "Content-Type: " << r.content_type << "\r\n"
                << "Content-Length: " << r.payload.size() << "\r\n"
                << "\r\n";
  const std::string header = header_stream.str();
  if (status s = co_await client.write(header); s.failure()) {
    co_return error{std::move(s)};
  }
  co_return co_await client.write(r.payload);
}

future<status> send_response(tcp::stream& client, const error& e) noexcept {
  std::ostringstream body_stream;
  body_stream << e;
  const std::string body = body_stream.str();
  // Note: we might consider replacing this with "return", but we can't since
  // the lifetime of body must outlive the execution of the coroutine.
  co_return co_await send_response(
      client, code(e),
      http_response{.payload = body, .content_type = "text/plain"});
}

future<result<http_request>> read_request(tcp::stream& client,
                                          span<char> buffer) noexcept {
  // Read the request line.
  result<request_line> request_line = co_await read_request_line(client);
  if (request_line.failure()) co_return error{std::move(request_line).status()};
  // Read the header fields.
  int content_length = 0;
  while (true) {
    result<std::string_view> line = co_await read_line(client, buffer);
    if (line.failure()) co_return error{std::move(line).status()};
    if (line->empty()) break;
    result<header_pair> header = parse_header_pair(*line);
    if (header.failure()) co_return error{std::move(header).status()};
    if (header->name == "Content-Length") {
      result<int> value = parse_int(header->value);
      if (value.failure()) co_return error{std::move(value).status()};
      content_length = *value;
    } else if (header->name == "Transfer-Encoding") {
      // TODO: Implement chunked transfer.
      co_return error{http_status::not_implemented};
    }
  }
  // Read the request payload.
  if (content_length > buffer.size()) {
    co_return error{http_status::payload_too_large};
  }
  const span<char> request_body = buffer.subspan(0, content_length);
  if (status s = co_await client.read(request_body); s.failure()) {
    co_return error{std::move(s)};
  }
  co_return http_request{
      .method = request_line->method,
      .target = std::move(request_line->target),
      .payload = std::string_view(request_body.data(), request_body.size()),
  };
}

future<status> handle_connection(tcp::stream& client,
                                 const handler_map& handlers) noexcept {
  char buffer[65536];
  result<http_request> request = co_await read_request(client, buffer);
  if (request.failure()) {
    co_return co_await send_response(client,
                                     error{std::move(request).status()});
  }
  // Look up the handler for the request.
  const auto i = handlers.find(request->target.path);
  if (i == handlers.end()) {
    co_return co_await send_response(client, error{http_status::not_found});
  }
  bool responded = false;
  request->respond = [&](result<http_response> response) -> future<status> {
    assert(!responded);
    responded = true;
    if (response.success()) {
      co_return co_await send_response(client, code(response.status()),
                                       *response);
    } else {
      co_return co_await send_response(client,
                                       error{std::move(response).status()});
    }
  };
  co_await i->second(std::move(*request));
  assert(responded);
  co_return status_code::ok;
}

future<void> spawn_connection(tcp::stream client,
                              const handler_map& handlers) noexcept {
  status s = co_await handle_connection(client, handlers);
  s.success() ? std::cout << s << '\n' : std::cerr << s << '\n';
}

future<void> accept_loop(tcp::acceptor& server,
                         const handler_map& handlers) noexcept {
  while (true) {
    result<tcp::stream> client = co_await server.accept();
    if (client.failure()) {
      std::cerr << "accept: " << client.status() << '\n';
      co_return;
    }
    (void)spawn_connection(std::move(*client), handlers);
  }
}

}  // namespace

status make_status(http_status code) noexcept {
  return http_status_manager.make(code);
}

status make_status(http_status code, std::string message) noexcept {
  return http_status_with_message_manager.make(code, std::move(message));
}

std::ostream& operator<<(std::ostream& output, http_method method) noexcept {
  switch (method) {
    case http_method::get: return output << "GET";
    case http_method::post: return output << "POST";
  }
  return output << "<unknown method>";
}

result<uri> parse_uri(std::string_view input) noexcept {
  // TODO: Determine if std::regex is good enough. If not, replace this with
  // a hand-coded implementation.
  static const std::regex pattern(
      R"(^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)",
      std::regex_constants::optimize | std::regex_constants::extended);
  std::cmatch match;
  if (!std::regex_match(input.data(), input.data() + input.size(), match,
                        pattern)) {
    return error{status(http_status::bad_request, "cannot parse URI")};
  }
  return uri{match[2], match[4], match[5], match[7], match[9]};
}

result<http_server> http_server::create(io_context& context,
                                        const address& address) noexcept {
  http_server server(context);
  if (status s = server.init(address); s.failure()) return error{std::move(s)};
  return server;
}

http_server::http_server(io_context& context) noexcept : context_(&context) {}

status http_server::init(const address& address) noexcept {
  result<tcp::acceptor> acceptor = tcp::bind(*context_, address);
  if (acceptor.failure()) return error{std::move(acceptor).status()};
  acceptor_ = std::move(*acceptor);
  return status_code::ok;
}

void http_server::handle(std::string path, handler h) noexcept {
  handlers_.try_emplace(std::move(path), std::move(h));
}

void http_server::start() noexcept {
  (void)accept_loop(acceptor_, handlers_);
}

}  // namespace util
