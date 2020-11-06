#include "util/http.h"
#include "util/io.h"
#include "util/result.h"

#include <iostream>
#include <fstream>

struct static_asset {
  const char* mime_type;
  const char* path;
};

constexpr static_asset assets[] = {
  {"image/x-icon", "/favicon.ico"},
};

int main() {
  util::address a;
  if (util::status s = a.init("::0", "8000"); s.failure()) {
    std::cerr << "Could not resolve server address: " << s << '\n';
    return 1;
  }
  util::http_server server;
  if (util::status s = server.init(std::move(a)); s.success()) {
    std::cout << "Serving on " << a << '\n';
  } else {
    std::cerr << "Failed to bind to " << a << ": " << s << '\n';
    return 1;
  }
  // Load assets.
  for (const auto [mime_type, path] : assets) {
    std::string_view data =
        util::contents((std::string("assets") + path).c_str());
    server.handle(path, [data, mime_type](util::http_request request) {
                          request.respond(util::http_response{data, mime_type});
                        });
  }
  server.handle("/", [](util::http_request request) {
    request.respond(util::http_response{"Hello, World!", "text/plain"});
  });
  if (util::status s = server.run(); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
}
