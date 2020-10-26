#include "util/http.h"
#include "util/result.h"

#include <iostream>

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
  if (util::status s = server.run(); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
}
