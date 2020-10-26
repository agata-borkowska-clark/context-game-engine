#include "http.h"

#include "status_managers.h"

#include <charconv>
#include <regex>

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
    http_status status{code(payload)};
    switch (status) {
      case http_status::ok:
        return "ok";
      case http_status::bad_request:
        return "bad_request";
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

constexpr char canned_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "\r\n"
    "Hello, World!";

struct header_data {
  span<char> header;
  span<char> trailing_bytes;
};

// Asynchronously read a HTTP header. Returns a header_data struct containing
// two spans: the first span is the header, while the second span is any
// trailing bytes which were not part of the header. The caller must consider
// the trailing bytes as a prefix for any subsequent reads.
void read_request_header(
    stream& client, span<char> buffer,
    std::function<void(result<header_data>)> done) noexcept {
  struct reader {
    stream& client;
    span<char> buffer;
    std::function<void(result<header_data>)> done;
    span<char>::size_type bytes_read = 0;
    int trailing_newlines = 0;
    void read() noexcept {
      auto free_space = buffer.subspan(bytes_read);
      if (free_space.empty()) {
        done(error{http_status::request_header_fields_too_large});
      } else {
        client.read_some(free_space, *this);
      }
    }
    void operator()(result<span<char>> bytes) noexcept {
      if (bytes.success()) {
        // Scan the new input for two consecutive newline characters. '\r' is
        // ignored, so the code will accept both '\r\n\r\n' and '\n\n'.
        for (char& c : *bytes) {
          switch (c) {
            case '\r':
              break;
            case '\n':
              trailing_newlines++;
              if (trailing_newlines == 2) {
                char* begin = buffer.data();
                char* end = &c + 1;
                span<char> header(begin, end - begin);
                span<char> trailing(end, buffer.end() - end);
                done(header_data{header, trailing});
                return;
              }
              break;
            default:
              trailing_newlines = 0;
              break;
          }
        }
        // The end of the header was not found, so we need to keep reading.
        read();
      } else {
        done(error{std::move(bytes).status()});
      }
    }
  };
  reader{client, buffer, std::move(done)}.read();
}

struct uri {
  std::string scheme;
  std::string authority;
  std::string path;
  std::string query;
  std::string fragment;
};

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

struct request_line {
  http_method method;
  uri target;
};

// Parse an HTTP method.
result<http_method> parse_method(std::string_view method) noexcept {
  if (method == "GET") return http_method::get;
  if (method == "POST") return http_method::post;
  return error{status(http_status::bad_request, "unknown method")};
}

// Parse an HTTP request line from a string_view.
result<request_line> parse_request_line(std::string_view line) noexcept {
  const char* const first = line.data();
  const char* const last = line.data() + line.size();
  const char* const method_end = std::find(first, last, ' ');
  if (method_end == last) {
    return error{status(http_status::bad_request, "cannot parse request line")};
  }
  result<http_method> method =
      parse_method(std::string_view(first, method_end - first));
  if (method.failure()) return error{std::move(method).status()};
  const char* const uri_begin = method_end + 1;
  const char* const uri_end = std::find(uri_begin, last, ' ');
  result<uri> uri = parse_uri(std::string_view(uri_begin, uri_end - uri_begin));
  if (uri.failure()) return error{std::move(uri).status()};
  return request_line{*method, std::move(*uri)};
}

struct header {
  std::string_view name;
  std::string_view value;
};

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

// Parse a single HTTP header from a string_view.
result<header> parse_header(std::string_view line) noexcept {
  assert(!line.empty());
  const char* const first = line.data();
  const char* const last = line.data() + line.size();
  const char* const header_name_end = std::find(first, last, ':');
  if (header_name_end == last) {
    return error{status(http_status::bad_request,
                        "cannot parse header in " + std::string(line))};
  }
  const std::string_view header_name(first, header_name_end - first);
  if (header_name.empty() || is_whitespace(header_name.front()) ||
      is_whitespace(header_name.back())) {
    // Leading whitespace is for obsolete line folding. Trailing whitespace is
    // forbidden.
    return error{status(http_status::bad_request, "whitespace in header name")};
  }
  const std::string_view header_value =
      trim(std::string_view(header_name_end + 1, last - header_name_end - 1));
  return header{header_name, header_value};
}

struct request_header : request_line {
  int content_length;
};

// Parse a full HTTP request header.
// TODO: Add support for custom headers.
result<request_header> parse_request_header(std::string_view header) noexcept {
  assert(!header.empty());
  assert(header.back() == '\n');
  const char* const first = header.data();
  const char* const last = header.data() + header.size();
  const char* i = std::find(first, last, '\n');
  auto request_line = parse_request_line(std::string_view(first, i - first));
  if (request_line.failure()) return error{std::move(request_line).status()};
  int content_length = 0;
  while (true) {
    // Parse a single `Header-Name: value` pair.
    const char* const line_start = i + 1;
    const char* const line_end = std::find(line_start, last, '\n');
    const std::string_view line =
        trim(std::string_view(line_start, line_end - line_start));
    if (line.empty()) break;
    i = line_end;
    auto header = parse_header(line);
    if (header.failure()) return error{std::move(header).status()};
    // Header names should be case insensitive, so make the name lowercase.
    std::string header_name;
    for (char c : header->name) header_name.push_back(std::tolower(c));
    // Handle known headers.
    if (header_name == "content-length") {
      const char* const value_begin = header->value.data();
      const char* const value_end = value_begin + header->value.size();
      const auto [ptr, code] =
          std::from_chars(value_begin, value_end, content_length);
      if (ptr != value_end || code != std::errc{}) {
        return error{status(http_status::bad_request, "bad content-length")};
      }
    } else if (header_name == "transfer-encoding") {
      // TODO: Implement chunked transfer.
      return error{http_status::not_implemented};
    }
  }
  return request_header{std::move(*request_line), content_length};
}

struct request : request_line {
  span<char> payload;
};

void read_request(stream& client, span<char> buffer,
                  std::function<void(result<request>)> done) noexcept {
  read_request_header(
      client, buffer,
      [&client, buffer, done = std::move(done)](result<header_data> data) {
        if (data.failure()) {
          done(error{std::move(data).status()});
          return;
        }
        auto header = parse_request_header(
            std::string_view(data->header.data(), data->header.size()));
        if (header.failure()) {
          done(error{std::move(header).status()});
          return;
        }
        if (header->content_length > buffer.size()) {
          done(error{http_status::payload_too_large});
          return;
        }
        // Read the payload of the request.
        const auto already_read = std::min<span<char>::size_type>(
            data->trailing_bytes.size(), header->content_length);
        std::memmove(buffer.data(), data->trailing_bytes.data(), already_read);
        const span<char>::size_type remaining =
            header->content_length - already_read;
        if (remaining == 0) {
          // Payload was already received.
          done(request{std::move(*header),
                       buffer.subspan(0, header->content_length)});
        } else {
          // Read the remainder of the payload.
          client.read(
              buffer.subspan(already_read, remaining),
              [&client, buffer, header = std::move(*header),
               done = std::move(done)](result<span<char>> payload) mutable {
                if (payload.success()) {
                  done(request{std::move(header),
                               buffer.subspan(0, header.content_length)});
                } else {
                  done(error{std::move(payload).status()});
                }
              });
        }
      });
}

struct connection {
  static void spawn(stream client) noexcept {
    auto self = std::make_shared<connection>(std::move(client));
    read_request(self->client, self->buffer, [self](result<request> r) {
      if (r.success()) {
        self->respond(self, std::move(*r));
      } else {
        std::cerr << r.status() << '\n';
      }
    });
  }

  connection(stream client) noexcept : client(std::move(client)) {}

  void respond(std::shared_ptr<connection> self, request r) noexcept {
    // TODO: Replace this dummy handling with sensible logic.
    std::cout << r.method << ' ' << r.target.path << '\n';
    std::string message = "Hello from " + r.target.path + "!";
    int size = std::sprintf(buffer,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: text/plain\r\n"
                            "Content-Length: %d\r\n"
                            "\r\n"
                            "%s",
                            (int)message.size(), message.c_str());
    if (size < 0) {
      std::cerr << "sprintf error :(\n";
      return;
    }
    client.write(span<const char>(buffer).subspan(0, size), [self](status s) {
      if (s.failure()) {
        std::cerr << s << '\n';
      }
    });
  }

  char buffer[65536];
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

std::ostream& operator<<(std::ostream& output, http_method method) noexcept {
  switch (method) {
    case http_method::get: return output << "GET";
    case http_method::post: return output << "POST";
  }
  return output << "<unknown method>";
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
