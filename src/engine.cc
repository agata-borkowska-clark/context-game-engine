#include "util/http.h"
#include "util/result.h"

#include <iostream>
#include <fstream>

std::string contents(const char* filename) noexcept {
  std::ifstream file(filename);
  if (!file.good()) {
    std::cerr << "Could not open " << filename << ".\n";
    std::exit(EXIT_FAILURE);
  }
  return std::string{std::istreambuf_iterator<char>(file), {}};
}

int main() {
  util::address a;
  if (util::status s = a.init("0.0.0.0", "8000"); s.failure()) {
    std::cerr << "Could not resolve server address: " << s << '\n';
    return 1;
  }
  util::http_server server;
  if (util::status s = server.init(std::move(a)); s.failure()) {
    std::cerr << "Failed to bind server: " << s << '\n';
    return 1;
  }
  // Load the favicon.
  const std::string favicon = contents("assets/favicon.ico");
  server.handle("/favicon.ico", [favicon](util::http_request request) {
    request.respond(util::http_response{favicon, "image/x-icon"});
  });
  server.handle("/", [](util::http_request request) {
    request.respond(util::http_response{"Hello, World!", "text/plain"});
  });
  if (util::status s = server.run(); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
}
