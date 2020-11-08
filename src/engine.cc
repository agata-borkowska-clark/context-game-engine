#include "util/http.h"
#include "util/io.h"
#include "util/result.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>

using std::literals::operator""s;

struct static_asset {
  const char* mime_type;
  const char* path;
};

constexpr static_asset assets[] = {
  {"image/x-icon", "/favicon.ico"},
};

static auto serve_static(const char* mime_type, const char* file_path) {
  std::string_view data = util::contents(file_path);
  return [mime_type, data](util::http_request request) -> util::future<void> {
    co_await request.respond(util::http_response{data, mime_type});
  };
}

int main() {
  util::address a;
  if (util::status s = a.init("::0", "8000"); s.failure()) {
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
    if (entry.path().extension() == ".js") {
      server.handle(("/"s + entry.path().c_str()).c_str(),
                    serve_static("text/javascript", entry.path().c_str()));
    }
  }
  server.handle("/", serve_static("text/html", "static/index.html"));
  server.start();
  if (util::status s = context.run(); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
}
