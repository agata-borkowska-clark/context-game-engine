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

// State for pending IO operations in an IO context. See the functions in
// io_context for more information.
struct io_state {
  using task = executor::task;
  file_handle handle;
  task do_in;
  task do_out;
};

class io_context : public executor {
 public:
  // Equivalent to constructing an io_context and calling init().
  static result<io_context> create() noexcept;

  // Construct an uninitialized io_context.
  io_context() noexcept;
  // Initialize the io_context. This must be called before any other operation
  // is performed.
  status init() noexcept;

  // Schedule a task to run in this context.
  void schedule_at(time_point, task) noexcept override;

  // Run work in this io_context.
  status run();

  // IO state is maintained in io_state objects. Before any IO can be performed
  // for a file handle, an io_state must be registered. Once IO for a file
  // handle is complete, it must be unregistered.
  status register_handle(io_state& state) noexcept;
  status unregister_handle(file_handle handle) noexcept;

  // Await an operation on this file stream. The state must have been registered
  // before these functions are called. The provided task will be scheduled as
  // soon as the file handle is ready to perform the corresponding operation, so
  // this can be used to schedule a read()/accept()/write() call for later.
  status await_in(io_state& state, io_state::task) noexcept;
  status await_out(io_state& state, io_state::task) noexcept;

 private:
  struct work_item {
    time_point time;
    task resume;
  };

  io_context(unique_handle epoll) noexcept;

  unique_handle epoll_;
  std::vector<work_item> work_;
};

class address_internals;

class address {
 public:
  static result<address> create(const char* host, const char* service) noexcept;
  address() noexcept;
  ~address() noexcept;

  status init(const char* host, const char* service) noexcept;

  // Non-copyable.
  address(const address&) = delete;
  address& operator=(const address&) = delete;

  // Movable.
  address(address&&) noexcept;
  address& operator=(address&&) noexcept;

  explicit operator bool() const noexcept;
  result<std::string> to_string() const noexcept;

 private:
  friend class address_internals;

  address(void* data) noexcept;

  void* data_;
};

std::ostream& operator<<(std::ostream& output, const address& a);

// Base socket class for raw sockets. This class does not expose any of the
// socket functionality: for that you want acceptor or stream from below.
class socket {
 public:
  // Equivalent to constructing a socket and calling init().
  static result<socket> create(io_context& context,
                               unique_handle handle) noexcept;

  // Construct an empty socket.
  socket() noexcept;

  // Initialise the socket. Must only be called on empty sockets.
  status init(io_context& context, unique_handle handle) noexcept;

  ~socket() noexcept;

  // Not copyable.
  socket(const socket&) = delete;
  socket& operator=(const socket&) = delete;

  // Movable.
  socket(socket&& other) noexcept = default;
  socket& operator=(socket&& other) noexcept = default;

  // Check if the socket is initialised (non-empty).
  explicit operator bool() const noexcept;
  file_handle handle() const noexcept;

  // Access the context and state for this socket. Only valid if the socket
  // object is non-empty.
  io_context& context() const noexcept;
  io_state& state() const noexcept;

  status shutdown() noexcept;

 private:
  socket(io_context&, unique_handle, std::unique_ptr<io_state>) noexcept;

  io_context* context_;
  unique_handle handle_;
  std::unique_ptr<io_state> state_;
};

namespace tcp {

// A TCP socket, supporting sequential reads and writes.
class stream {
 public:
  stream() noexcept;
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

  // Check if the socket is initialised (non-empty).
  explicit operator bool() const noexcept;

  // Access the context and state for this socket. Only valid if the socket
  // object is non-empty.
  io_context& context() const noexcept;

 private:
  socket socket_;
};

// A TCP server, accepting TCP sockets.
class acceptor {
 public:
  static constexpr int max_pending_connections = 8;

  acceptor() noexcept;
  acceptor(socket socket) noexcept;

  // Asynchronously accept a new connection. On success, returns the newly
  // established stream. On failure, returns an error code explaining what went
  // wrong.
  void accept(std::function<void(result<stream>)> done) noexcept;

  // Check if the socket is initialised (non-empty).
  explicit operator bool() const noexcept;

  // Access the context and state for this socket. Only valid if the socket
  // object is non-empty.
  io_context& context() const noexcept;

 private:
  socket socket_;
};

// Host: bind an acceptor to the given address.
result<acceptor> bind(io_context&, const address&);

// Client: connect a stream to the given address.
result<stream> connect(io_context&, const address&);

}  // namespace tcp

}  // namespace util
