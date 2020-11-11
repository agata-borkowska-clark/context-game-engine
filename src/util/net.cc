#include "net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace util {
namespace {

using std::chrono_literals::operator""ms;

// Order time points in *descending* order so that they are put in *ascending*
// order in a heap.
constexpr auto by_time = [](auto& l, auto& r) {
  return l.time > r.time;
};

void must(const status& status) {
#ifndef NDEBUG
  // In debug builds only, crash the application if the given operation does not
  // succeed.
  if (!status.success()) {
    std::cerr << status << '\n';
    std::abort();
  }
#endif
}

// Shared implementation for await_in and await_out: register interest for an IO
// operation in the epoll instance.
template <io_state::task io_state::*op>
status await_op(file_handle epoll, io_state& state,
                io_state::task resume) noexcept {
  state.*op = std::move(resume);
  epoll_event event;
  event.events = EPOLLONESHOT | (state.do_in ? EPOLLIN : 0) |
                 (state.do_out ? EPOLLOUT : 0);
  event.data.ptr = &state;
  if (epoll_ctl((int)epoll, EPOLL_CTL_MOD, (int)state.handle, &event) == -1) {
    return std::errc{errno};
  } else {
    return status_code::ok;
  }
}

// Base status manager for get_address_info codes.
struct gai_code_manager : status_manager {
  constexpr std::uint64_t domain_id() const noexcept final {
    // Randomly chosen bytes.
    return 0x42'db'f4'41'b0'62'a2'b1;
  }
  constexpr std::string_view domain() const noexcept final {
    return "address_info";
  }
  std::string_view name(status_payload payload) const noexcept final {
    return gai_strerror(code(payload));
  }
  bool failure(status_payload payload) const noexcept final {
    return code(payload) != 0;
  }
  status_code canonical(status_payload payload) const noexcept final {
    switch (code(payload)) {
      case 0: return status_code::ok;
      case EAI_ADDRFAMILY: return status_code::client_error;
      case EAI_AGAIN: return status_code::transient_error;
      case EAI_BADFLAGS: return status_code::client_error;
      case EAI_FAIL: return status_code::permanent_error;
      case EAI_FAMILY: return status_code::not_available;
      case EAI_MEMORY: return status_code::transient_error;
      case EAI_NODATA: return status_code::not_available;
      case EAI_NONAME: return status_code::client_error;
      case EAI_SERVICE: return status_code::not_available;
      case EAI_SOCKTYPE: return status_code::not_available;
      case EAI_SYSTEM: return status_code::unknown_error;
      default: return status_code::unknown_error;
    }
  }
  constexpr int code(status_payload payload) const noexcept final{
    return payload.code;
  }
  void output(std::ostream&, status_payload) const noexcept final {}
  void destroy(status_payload) const noexcept final {}
};
static constexpr gai_code_manager gai_code_manager;

// Build an error object from a get_address_info error code.
error gai_error(int code) {
  if (code == EAI_SYSTEM) {
    // When the code is EAI_SYSTEM, the more detailed error is in errno.
    return error{std::errc{errno}};
  } else {
    status_payload payload;
    payload.code = code;
    return error{status(gai_code_manager, payload)};
  }
}

// Retrieve the address for a socket.
template <auto get_socket_address>
result<address> get_address(const socket& socket) noexcept {
  // Retrieve the socket address information.
  sockaddr_storage raw_storage;
  sockaddr* storage = reinterpret_cast<sockaddr*>(&raw_storage);
  socklen_t size = sizeof(raw_storage);
  if (get_socket_address((int)socket.handle(), storage, &size) == -1) {
    if (errno == ENOTCONN) return {};
    return error{std::errc{errno}};
  }
  // Convert the address into human-readable host and port strings.
  char host[1024], decimal_port[8];
  int status = getnameinfo(storage, size, host, sizeof(host), decimal_port,
                           sizeof(decimal_port), NI_NUMERICSERV);
  if (status != 0) throw gai_error(status);
  return address{host, static_cast<std::uint16_t>(std::atoi(decimal_port))};
}

// RAII wrapper for addrinfo objects.
class addr_info {
 public:
  // Resolve an address into an addrinfo list.
  static result<addr_info> resolve(const char* address,
                                   const char* service) noexcept {
    addrinfo* info;
    int status = getaddrinfo(address, service, nullptr, &info);
    if (status != 0) return gai_error(status);
    return addr_info{info};
  }

  constexpr addr_info(addrinfo* info) noexcept : value_(info) {}
  ~addr_info() noexcept {
    if (value_) freeaddrinfo(value_);
  }

  // Non copyable.
  addr_info(const addr_info&) = delete;
  addr_info& operator=(const addr_info&) = delete;

  // Movable.
  addr_info(addr_info&& other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}

  addr_info& operator=(addr_info&& other) noexcept {
    if (value_) freeaddrinfo(value_);
    value_ = std::exchange(other.value_, nullptr);
    return *this;
  }

  constexpr addrinfo* get() const { return value_; }

 private:
  addrinfo* value_;
};

// Change a file handle to non-blocking mode.
status set_non_blocking(socket& socket) {
  int flags = fcntl((int)socket.handle(), F_GETFL);
  if (flags == -1) return std::errc{errno};
  if (fcntl((int)socket.handle(), F_SETFL, flags | O_NONBLOCK) == -1) {
    return std::errc{errno};
  }
  return status_code::ok;
}

// Allow a socket to rebind to the same port.
status allow_address_reuse(socket& socket) {
  int enable = 1;
  int r = setsockopt((int)socket.handle(), SOL_SOCKET, SO_REUSEADDR, &enable,
                     sizeof(int));
  if (r == -1) return std::errc{errno};
  return status_code::ok;
}

struct host_port {
  char host[248];
  char port[8];
};

result<host_port> get_host_port(const addrinfo* info) noexcept {
  host_port out;
  const int status =
      getnameinfo(info->ai_addr, info->ai_addrlen, out.host, sizeof(out.host),
                  out.port, sizeof(out.port), NI_NUMERICSERV);
  if (status != 0) return gai_error(status);
  return out;
}

}  // namespace

unique_handle::unique_handle() noexcept : handle_(file_handle::none) {}

unique_handle::unique_handle(file_handle handle) noexcept
    : handle_(handle) {}

unique_handle::~unique_handle() noexcept { must(close()); }

unique_handle::unique_handle(unique_handle&& other) noexcept
    : handle_(std::exchange(other.handle_, file_handle::none)) {}

unique_handle& unique_handle::operator=(unique_handle&& other) noexcept {
  must(close());
  handle_ = std::exchange(other.handle_, file_handle::none);
  return *this;
}

file_handle unique_handle::get() const noexcept { return handle_; }
unique_handle::operator bool() const noexcept {
  return handle_ != file_handle::none;
}

status unique_handle::close() noexcept {
  if (handle_ == file_handle::none) return status_code::ok;
  if (::close((int)handle_) == -1) {
    return status(std::errc{errno}, "from close() in unique_handle::close()");
  }
  return status_code::ok;
}

result<io_context> io_context::create() noexcept {
  io_context context;
  if (status s = context.init(); s.failure()) return error{std::move(s)};
  return context;
}

io_context::io_context() noexcept {}

status io_context::init() noexcept {
  epoll_ = unique_handle{file_handle{epoll_create(/*unused size*/42)}};
  if (!epoll_) {
    return error{
        status(std::errc{errno}, "from epoll_create in io_context::create()")};
  }
  return status_code::ok;
}

io_context::io_context(unique_handle epoll) noexcept
    : epoll_(std::move(epoll)) {}

void io_context::schedule_at(time_point time, task f) noexcept {
  work_.push_back({time, std::move(f)});
  std::push_heap(work_.begin(), work_.end(), by_time);
}

status io_context::run() {
  // TODO: Find a neat way of tracking how many pending IO operations the
  // context has and use this to allow run() to return when all work finishes.
  while (true) {
    // Run all work that should have triggered by now.
    const time_point now = clock::now();
    while (!work_.empty() && work_.front().time <= now) {
      std::pop_heap(work_.begin(), work_.end(), by_time);
      work_item work = std::move(work_.back());
      work_.pop_back();
      work.resume();
    }
    // Wait for IO until the next work item is ready.
    const int timeout_ms =
        work_.empty() ? -1 : (work_.front().time - now) / 1ms;
    std::array<epoll_event, 256> events;
    const int num_events =
        epoll_wait((int)epoll_.get(), events.data(), events.size(), timeout_ms);
    if (num_events == -1 && errno != EINTR) {
      return error{
          status(std::errc{errno}, "from epoll_wait in io_context::run()")};
    }
    for (int i = 0; i < num_events; i++) {
      auto& state = *static_cast<io_state*>(events[i].data.ptr);
      unsigned mask = events[i].events;
      // If an error occurred or the socket was closed, treat it as both read
      // and write being ready. The handlers will attempt their operations and
      // discover the errors themselves.
      if (mask & (EPOLLERR | EPOLLHUP)) mask |= EPOLLIN | EPOLLOUT;
      // Run handlers that are ready. These are scheduled rather than being
      // invoked directly to avoid having to deal with reentrancy.
      if ((mask & EPOLLIN) && state.do_in) {
        schedule(std::exchange(state.do_in, nullptr));
      }
      if ((mask & EPOLLOUT) && state.do_out) {
        schedule(std::exchange(state.do_out, nullptr));
      }
      mask = (state.do_in ? EPOLLIN : 0) | (state.do_out ? EPOLLOUT : 0);
      if (mask) {
        // There are other pending I/O handlers. Update the event entry.
        events[i].events = EPOLLONESHOT | mask;
        if (epoll_ctl((int)epoll_.get(), EPOLL_CTL_MOD, (int)state.handle,
                      &events[i]) == -1) {
          return error{
              status(std::errc{errno}, "from epoll_ctl in io_context::run()")};
        }
      }
    }
  }
}

status io_context::register_handle(io_state& state) noexcept {
  epoll_event event;
  event.events = 0;
  event.data.ptr = &state;
  if (epoll_ctl((int)epoll_.get(), EPOLL_CTL_ADD, (int)state.handle, &event) ==
      -1) {
    return status(std::errc{errno}, "in io_context::register_handle()");
  }
  return status_code::ok;
}

status io_context::unregister_handle(file_handle handle) noexcept {
  if (epoll_ctl((int)epoll_.get(), EPOLL_CTL_DEL, (int)handle, nullptr) == -1) {
    return status(std::errc{errno}, "in io_context::unregister_handle()");
  }
  return status_code::ok;
}

status io_context::await_in(io_state& state, io_state::task resume) noexcept {
  return await_op<&io_state::do_in>(epoll_.get(), state, std::move(resume));
}

status io_context::await_out(io_state& state, io_state::task resume) noexcept {
  return await_op<&io_state::do_out>(epoll_.get(), state, std::move(resume));
}

class address_internals {
 public:
  static addrinfo* get(const address& address) noexcept {
    return (addrinfo*)address.data_;
  }
};

result<address> address::create(const char* host,
                                const char* service) noexcept {
  address a;
  if (status s = a.init(host, service); s.failure()) return error{std::move(s)};
  return a;
}

status address::init(const char* host, const char* service) noexcept {
  addrinfo* info;
  const int status = getaddrinfo(host, service, nullptr, &info);
  if (status != 0) return gai_error(status);
  if (data_) freeaddrinfo((addrinfo*)data_);
  data_ = info;
  return status_code::ok;
}

address::address() noexcept : data_(nullptr) {}

address::~address() noexcept {
  if (data_) freeaddrinfo((addrinfo*)data_);
}

address::address(address&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)) {}

address& address::operator=(address&& other) noexcept {
  if (data_) freeaddrinfo((addrinfo*)data_);
  data_ = std::exchange(other.data_, nullptr);
  return *this;
}

address::operator bool() const noexcept { return data_; }

result<std::string> address::to_string() const noexcept {
  const auto* a = (addrinfo*)data_;
  result<host_port> temp = get_host_port(a);
  if (temp.failure()) return error{std::move(temp).status()};
  if (a->ai_family == AF_INET6) {
    return "[" + std::string(temp->host) + "]:" + temp->port;
  } else {
    return std::string(temp->host) + ":" + temp->port;
  }
}

std::ostream& operator<<(std::ostream& output, const address& a) {
  if (!a) {
    return output << "(no address)";
  } else if (auto x = a.to_string(); x.success()) {
    return output << *x;
  } else {
    return output << "(" << x.status() << ")";
  }
}

result<socket> socket::create(io_context& context,
                              unique_handle handle) noexcept {
  socket out;
  if (status s = out.init(context, std::move(handle)); s.failure()) {
    return error{std::move(s)};
  }
  return out;
}

socket::socket() noexcept {}

status socket::init(io_context& context, unique_handle handle) noexcept {
  if (!handle) return client_error("cannot init a socket with an empty handle");
  context_ = &context;
  handle_ = std::move(handle);
  state_ = std::make_unique<io_state>();
  state_->handle = handle_.get();
  if (status s = context.register_handle(*state_); !s.success()) {
    return error{std::move(s)};
  }
  return status_code::ok;
}

socket::~socket() noexcept {
  if (handle_) {
    must(context_->unregister_handle(handle_.get()));
    if (status s = shutdown();
        s.failure() && s != status{std::errc{std::errc::not_connected}}) {
      must(s);
    }
  }
}

socket::operator bool() const noexcept { return (bool)handle_; }
file_handle socket::handle() const noexcept { return handle_.get(); }
io_context& socket::context() const noexcept { return *context_; }
io_state& socket::state() const noexcept { return *state_; }

status socket::shutdown() noexcept {
  if (::shutdown((int)handle_.get(), SHUT_RDWR) == -1) {
    return status(std::errc{errno}, "in socket::shutdown()");
  } else {
    return status_code::ok;
  }
}

socket::socket(io_context& context, unique_handle handle,
               std::unique_ptr<io_state> state) noexcept
    : context_(&context),
      handle_(std::move(handle)),
      state_(std::move(state)) {}

namespace tcp {

stream::stream() noexcept {}
stream::stream(socket socket) noexcept
    : socket_(std::move(socket)) {}

promise<result<span<char>>> stream::read_some(span<char> buffer) noexcept {
  return promise<result<span<char>>>([&](auto& promise) {
    auto& state = socket_.state();
    auto handler = [&state, buffer, &promise] {
      int result = ::read((int)state.handle, buffer.data(), buffer.size());
      if (result != -1) {
        promise.resolve(buffer.subspan(0, result));
      } else {
        promise.resolve(error{std::errc{errno}});
      }
    };
    if (status s = socket_.context().await_in(state, std::move(handler));
        !s.success()) {
      promise.resolve(error{std::move(s)});
    }
  });
}

future<status> stream::read(span<char> buffer) noexcept {
  span<char> remaining = buffer;
  while (!remaining.empty()) {
    result<span<char>> x = co_await read_some(remaining);
    if (x.failure()) co_return error{std::move(x).status()};
    if (x->empty()) co_return error{status_code::exhausted};
    remaining = remaining.subspan(x->size());
  }
  co_return status_code::ok;
}

promise<result<span<const char>>> stream::write_some(
    span<const char> buffer) noexcept {
  return promise<result<span<const char>>>([&](auto& promise) {
    auto& state = socket_.state();
    auto handler = [&state, buffer, &promise] {
      int result = ::write((int)state.handle, buffer.data(), buffer.size());
      if (result != -1) {
        promise.resolve(buffer.subspan(result));
      } else {
        promise.resolve(error{std::errc{errno}});
      }
    };
    if (status s = socket_.context().await_out(state, std::move(handler));
        !s.success()) {
      promise.resolve(error{std::move(s)});
    }
  });
}

future<status> stream::write(span<const char> buffer) noexcept {
  span<const char> remaining = buffer;
  while (!remaining.empty()) {
    result<span<const char>> x = co_await write_some(remaining);
    if (x.failure()) co_return error{std::move(x).status()};
    if (x->size() == remaining.size()) co_return error{status_code::exhausted};
    remaining = *x;
  }
  co_return status_code::ok;
}

stream::operator bool() const noexcept { return (bool)socket_; }
io_context& stream::context() const noexcept { return socket_.context(); }

acceptor::acceptor() noexcept {}
acceptor::acceptor(socket socket) noexcept : socket_(std::move(socket)) {}

promise<result<stream>> acceptor::accept() noexcept {
  return promise<result<stream>>([&](auto& promise) {
    auto& state = socket_.state();
    auto handler = [&context = socket_.context(), &state, &promise] {
      int handle = accept4((int)state.handle, nullptr, nullptr, SOCK_NONBLOCK);
      if (handle == -1) {
        promise.resolve(error{std::errc{errno}});
        return;
      }
      result<socket> s =
          socket::create(context, unique_handle{file_handle{handle}});
      if (s.success()) {
        promise.resolve(std::move(*s));
      } else {
        promise.resolve(error{std::move(s).status()});
      }
    };
    if (status s = socket_.context().await_in(state, std::move(handler));
        !s.success()) {
      promise.resolve(error{std::move(s)});
    }
  });
}

acceptor::operator bool() const noexcept { return (bool)socket_; }
io_context& acceptor::context() const noexcept { return socket_.context(); }

result<acceptor> bind(io_context& context, const address& address) {
  const addrinfo* const info = address_internals::get(address);
  // Create a socket in the right address family (e.g. IPv4 or IPv6).
  result<socket> socket = socket::create(
      context,
      unique_handle{file_handle{::socket(info->ai_family, SOCK_STREAM, 0)}});
  if (socket.failure()) return error{std::move(socket).status()};
  // Switch the socket to non-blocking mode. This way if we try to perform any
  // blocking operation before it is ready it will fail immediately instead of
  // blocking.
  if (status s = set_non_blocking(*socket); s.failure()) {
    return error{std::move(s)};
  }
  // Allow binding to a previously used port. This allows us to shut down and
  // restart the server without waiting for a minute or so for the TCP timeout
  // to expire.
  if (status s = allow_address_reuse(*socket); s.failure()) {
    return error{std::move(s)};
  }
  // Bind to the address.
  const int bind_result =
      ::bind((int)socket->handle(), info->ai_addr, info->ai_addrlen);
  if (bind_result == -1) return error{std::errc{errno}};
  // Start listening for incoming connections.
  const int listen_result =
      ::listen((int)socket->handle(), acceptor::max_pending_connections);
  if (listen_result == -1) return error{std::errc{errno}};
  return acceptor{std::move(*socket)};
}

result<stream> connect(io_context& context, const address& address) {
  const addrinfo* const info = address_internals::get(address);
  // Create a socket in the right address family (e.g. IPv4 or IPv6).
  result<socket> socket = socket::create(
      context,
      unique_handle{file_handle{::socket(info->ai_family, SOCK_STREAM, 0)}});
  if (socket.failure()) return error{std::move(socket).status()};
  // Switch the socket to non-blocking mode. This way if we try to perform any
  // blocking operation before it is ready it will fail immediately instead of
  // blocking.
  if (status s = set_non_blocking(*socket); s.failure()) {
    return error{std::move(s)};
  }
  // Connect to the address.
  const int connect_result =
      ::connect((int)socket->handle(), info->ai_addr, info->ai_addrlen);
  if (connect_result == -1) return error{std::errc{errno}};
  return stream{std::move(*socket)};
}

}  // namespace tcp

}  // namespace util
