#include "util/http.h"
#include "util/io.h"
#include "util/result.h"
#include "util/websocket.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using std::literals::operator""s;

struct static_asset {
  const char* mime_type;
  const char* path;
};

constexpr static_asset assets[] = {
  {"image/x-icon", "/favicon.ico"},
};

auto serve_static(const char* mime_type, const char* file_path) {
  std::string_view data = util::contents(file_path);
  return [mime_type, data](util::http_request request) -> util::future<void> {
    co_await request.respond(util::http_response{data, mime_type});
  };
}

util::future<util::status> run_websocket(util::websocket& socket) noexcept {
  while (true) {
    char buffer[65536];
    util::result<util::websocket::message> message =
        co_await socket.receive_message(buffer);
    if (message.failure()) {
      co_return util::error{std::move(message).status()};
    }
    std::cout << "Received " << message->type << ":\n";
    std::cout.write(message->payload.data(), message->payload.size()) << '\n';
    if (util::status s = co_await socket.send_message(*message); s.failure()) {
      co_return util::error{std::move(s)};
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc > 3) {
    std::cerr << "Usage: engine [port | host port]\n";
    return 1;
  }
  const char* host = "0.0.0.0";
  const char* service = "8000";
  if (argc == 2) {
    service = argv[1];
  } else if (argc == 3) {
    host = argv[1];
    service = argv[2];
  }
  util::address a;
  if (util::status s = a.init(host, service); s.failure()) {
    std::cerr << "Could not resolve server address: " << s << '\n';
    return 1;
  }
  util::io_context context;
  if (util::status s = context.init(); s.failure()) {
    std::cerr << "Could not initialize IO context: " << s << '\n';
    return 1;
  }
  util::http_server server(context);
  if (util::status s = server.init(std::move(a)); s.success()) {
    std::cout << "Serving on " << a << '\n';
  } else {
    std::cerr << "Failed to bind to " << a << ": " << s << '\n';
    return 1;
  }
  // Load assets.
  for (const auto [mime_type, path] : assets) {
    server.handle(path, serve_static(mime_type, ("static"s + path).c_str()));
  }
  for (const auto entry :
       std::filesystem::recursive_directory_iterator("scripts")) {
    constexpr const char* mime_types[][2] = {
      {".js", "text/javascript"},
      {".map", "application/octet-stream"},
      {".ts", "application/typescript"},
    };
    for (const auto [extension, mime_type] : mime_types) {
      if (entry.path().extension() == extension) {
        server.handle(("/"s + entry.path().c_str()).c_str(),
                      serve_static(mime_type, entry.path().c_str()));
      }
    }
  }
  server.handle("/", serve_static("text/html", "static/index.html"));
  server.handle("/demo", util::handle_websocket(run_websocket));
  server.start();
  if (util::status s = context.run(); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
}
