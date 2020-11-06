#include "io.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

namespace util {

std::string_view contents(const char* filename) noexcept {
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    std::cerr << "Cannot open file: " << filename << "\n";
    std::exit(EXIT_FAILURE);
  }
  struct stat info;
  if (fstat(fd, &info) < 0) {
    std::cerr << "Cannot stat file: " << filename << "\n";
    std::exit(EXIT_FAILURE);
  }
  const char* data =
      (const char*)mmap(nullptr, info.st_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);  // At this point we don't need the file descriptor any more.
  if (data == (caddr_t)-1) {
    std::cerr << "Cannot mmap file: " << filename << "\n";
    std::exit(EXIT_FAILURE);
  }
  return std::string_view(data, info.st_size);
}

}  // namespace util
