#pragma once

#include "executor.h"
#include "result.h"
#include "span.h"
#include "status.h"

#include <memory>

namespace util {

enum class file_handle : int { none = -1 };

// Smart file handle that automatically closes.
class unique_handle {
 public:
  unique_handle() noexcept;
  explicit unique_handle(file_handle handle) noexcept;
  ~unique_handle() noexcept;

  // Not copyable.
  unique_handle(const unique_handle&) = delete;
  unique_handle& operator=(const unique_handle&) = delete;

  // Movable.
  unique_handle(unique_handle&& other) noexcept;
  unique_handle& operator=(unique_handle&& other) noexcept;

  file_handle get() const noexcept;
  operator bool() const noexcept;

  status close() noexcept;

 private:
  file_handle handle_;
};

class socket;

struct io_state {
  using task = executor::task;
  file_handle handle;
  task do_in;
  task do_out;
};

class io_context : public executor {
 public:
  static result<io_context> create() noexcept;
  void schedule_at(time_point, task) noexcept override;
  status run();

  status register_handle(io_state& state) noexcept;
  status unregister_handle(file_handle handle) noexcept;

  status await_in(io_state& state, io_state::task) noexcept;
  status await_out(io_state& state, io_state::task) noexcept;

 private:
  friend class socket;

  struct work_item {
    time_point time;
    task resume;
  };

  io_context(unique_handle epoll) noexcept;

  unique_handle epoll_;
  std::vector<work_item> work_;
};

struct address {
  std::string host;
  std::uint16_t port;
};

std::ostream& operator<<(std::ostream&, const address&);

class socket {
 public:
  static result<socket> create(io_context& context,
                               unique_handle handle) noexcept;
  socket(io_context&) noexcept;
  socket(io_context&, unique_handle, std::unique_ptr<io_state>) noexcept;
  ~socket() noexcept;

  // Not copyable.
  socket(const socket&) = delete;
  socket& operator=(const socket&) = delete;

  // Movable.
  socket(socket&& other) noexcept = default;
  socket& operator=(socket&& other) noexcept = default;

  explicit operator bool() const noexcept;
  file_handle handle() const noexcept;

  // Access the context and state for this socket. Only valid if the socket
  // object is non-empty.
  io_context& context() const noexcept;
  io_state& state() const noexcept;

 private:
  io_context* context_;
  unique_handle handle_;
  std::unique_ptr<io_state> state_;
};

class stream {
 public:
  stream(socket socket) noexcept;

  // Asynchronously read data from the stream into the provided buffer. The
  // continuation function will be invoked either with a status describing the
  // failure or a non-empty span of bytes that were read.
  void read_some(span<char> buffer,
                 std::function<void(result<span<char>>)> done) noexcept;
  // Like read_some, but will keep trying until it fills the entire buffer or
  // gets an error.
  void read(span<char> buffer,
            std::function<void(result<span<char>>)> done) noexcept;

  // Asynchronously write data from the provided buffer to the stream. The
  // continuation function will be invoked either with a status describing the
  // failure or with a span of yet-to-be-written bytes which is at least one
  // byte smaller than the input.
  void write_some(span<const char> buffer,
                  std::function<void(result<span<const char>>)> done) noexcept;
  // Like write_some, but will keep trying until everything is written or an
  // error occurs.
  void write(span<const char> buffer,
             std::function<void(status)> done) noexcept;

  // Access the context and state for this socket. Only valid if the socket
  // object is non-empty.
  io_context& context() const noexcept;

 private:
  socket socket_;
};

class acceptor {
 public:
  static constexpr int max_pending_connections = 8;

  acceptor(socket socket) noexcept;

  // Asynchronously accept a new connection. On success, returns the newly
  // established stream. On failure, returns an error code explaining what went
  // wrong.
  void accept(std::function<void(result<stream>)> done) noexcept;

  // Access the context and state for this socket. Only valid if the socket
  // object is non-empty.
  io_context& context() const noexcept;

 private:
  socket socket_;
};

result<acceptor> bind(io_context&, const address&);
result<stream> connect(io_context&, const address&);

}  // namespace util
