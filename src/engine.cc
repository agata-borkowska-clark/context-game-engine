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
  util::http_server server;
  if (util::status s = server.init({"0.0.0.0", 8000}); s.success()) {
    std::cout << "Serving on 8000\n";
  } else if (util::status s2 = server.init({"0.0.0.0", 8080}); s2.success()) {
    std::cout << "Serving on 8080\n";
  } else {
    std::cerr << "Failed to bind to 8000 (" << s << ") or 8080 (" << s2
              << ").\n";
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
