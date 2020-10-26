#include "util/http.h"
#include "util/result.h"

#include <iostream>

int main() {
  util::http_server server;
  if (util::status s = server.init({"0.0.0.0", 8000}); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
  if (util::status s = server.run(); s.failure()) {
    std::cerr << s << '\n';
    return 1;
  }
}
