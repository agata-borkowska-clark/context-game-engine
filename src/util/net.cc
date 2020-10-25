#include "net.h"

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
  if (!status.success()) {
    std::cerr << status << '\n';
    std::abort();
  }
#endif
}

template <io_state::task io_state::*op>
status await_op(file_handle epoll, io_state& state,
                io_state::task resume) noexcept {
  state.*op = std::move(resume);
  epoll_event event;
  event.events = EPOLLONESHOT | (state.do_in ? EPOLLIN : 0) |
                 (state.do_out ? EPOLLOUT : 0);
  event.data.ptr = &state;
  if (epoll_ctl((int)epoll, EPOLL_CTL_MOD, (int)state.handle, &event) == -1) {
    return posix_status(errno);
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

error gai_error(int code) {
  if (code == EAI_SYSTEM) {
    return posix_error(errno);
  } else {
    status_payload payload;
    payload.code = code;
    return error{status(gai_code_manager, payload)};
  }
}

template <auto get_socket_address>
result<address> get_address(const socket& socket) noexcept {
  // Retrieve the socket address information.
  sockaddr_storage raw_storage;
  sockaddr* storage = reinterpret_cast<sockaddr*>(&raw_storage);
  socklen_t size = sizeof(raw_storage);
  if (get_socket_address((int)socket.handle(), storage, &size) == -1) {
    if (errno == ENOTCONN) return {};
    return posix_error(errno);
  }
  // Convert the address into human-readable host and port strings.
  char host[1024], decimal_port[8];
  int status = getnameinfo(storage, size, host, sizeof(host), decimal_port,
                           sizeof(decimal_port), NI_NUMERICSERV);
  if (status != 0) throw gai_error(status);
  return address{host, static_cast<std::uint16_t>(std::atoi(decimal_port))};
}

class addr_info {
 public:
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

status set_non_blocking(socket& socket) {
  int flags = fcntl((int)socket.handle(), F_GETFL);
  if (flags == -1) return posix_error(errno);
  if (fcntl((int)socket.handle(), F_SETFL, flags | O_NONBLOCK) == -1) {
    return posix_error(errno);
  }
  return status_code::ok;
}

}  // namespace

unique_handle::unique_handle() noexcept : handle_(file_handle::none) {}

unique_handle::unique_handle(file_handle handle) noexcept
    : handle_(handle) {}

unique_handle::~unique_handle() noexcept {
  if (handle_ != file_handle::none) {
    std::cout << "bye bye " << (int)handle_ << '\n';
  }
  must(close());
}

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
  if (::shutdown((int)handle_, SHUT_RDWR) == -1) {
    return posix_status(errno, "from shutdown() in unique_handle::close()");
  }
  if (::close((int)handle_) == -1) {
    return posix_status(errno, "from close() in unique_handle::close()");
  }
  return status_code::ok;
}

result<io_context> io_context::create() noexcept {
  unique_handle epoll{file_handle{epoll_create(/*unused size*/42)}};
  if (!epoll) {
    return error{
        posix_status(errno, "from epoll_create in io_context::create()")};
  }
  return io_context{std::move(epoll)};
}

io_context::io_context(unique_handle epoll) noexcept
    : epoll_(std::move(epoll)) {}

void io_context::schedule_at(time_point time, task f) noexcept {
  work_.push_back({time, std::move(f)});
  std::push_heap(work_.begin(), work_.end(), by_time);
}

status io_context::run() {
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
      return error{posix_status(errno, "from epoll_wait in io_context::run()")};
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
              posix_status(errno, "from epoll_ctl in io_context::run()")};
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
    return posix_status(errno, "in io_context::register_handle()");
  }
  return status_code::ok;
}

status io_context::unregister_handle(file_handle handle) noexcept {
  if (epoll_ctl((int)epoll_.get(), EPOLL_CTL_DEL, (int)handle, nullptr) == -1) {
    return posix_status(errno, "in io_context::unregister_handle()");
  }
  return status_code::ok;
}

status io_context::await_in(io_state& state, io_state::task resume) noexcept {
  return await_op<&io_state::do_in>(epoll_.get(), state, std::move(resume));
}

status io_context::await_out(io_state& state, io_state::task resume) noexcept {
  return await_op<&io_state::do_out>(epoll_.get(), state, std::move(resume));
}

std::ostream& operator<<(std::ostream& output, const address& a) {
  return output << a.host << ':' << a.port;
}

result<socket> socket::create(io_context& context,
                              unique_handle handle) noexcept {
  if (!handle) return result<socket>(socket(context));
  auto state = std::make_unique<io_state>();
  state->handle = handle.get();
  if (status s = context.register_handle(*state); !s.success()) {
    return error{std::move(s)};
  }
  return socket(context, std::move(handle), std::move(state));
}

socket::socket(io_context& context) noexcept : context_(&context) {}

socket::socket(io_context& context, unique_handle handle,
               std::unique_ptr<io_state> state) noexcept
    : context_(&context),
      handle_(std::move(handle)),
      state_(std::move(state)) {}

socket::~socket() noexcept {
  if (handle_) must(context_->unregister_handle(handle_.get()));
}

socket::operator bool() const noexcept { return (bool)handle_; }
file_handle socket::handle() const noexcept { return handle_.get(); }
io_context& socket::context() const noexcept { return *context_; }
io_state& socket::state() const noexcept { return *state_; }

stream::stream(socket socket) noexcept
    : socket_(std::move(socket)) {}

void stream::read_some(span<char> buffer,
                       std::function<void(result<span<char>>)> done) noexcept {
  auto& state = socket_.state();
  auto handler = [&state, buffer, done] {
    int result = ::read((int)state.handle, buffer.data(), buffer.size());
    if (result != -1) {
      done(buffer.subspan(0, result));
    } else {
      done(error{posix_status(errno)});
    }
  };
  if (status s = socket_.context().await_in(state, std::move(handler));
      !s.success()) {
    done(error{std::move(s)});
  }
}

void stream::read(span<char> buffer,
                  std::function<void(result<span<char>>)> done) noexcept {
  struct reader {
    stream* input;
    span<char> buffer;
    std::function<void(result<span<char>>)> done;
    span<char>::size_type bytes_read = 0;
    void run() {
      input->read_some(buffer.subspan(bytes_read), *this);
    }
    void operator()(result<span<char>> result) {
      if (result.failure()) {
        done(std::move(result));
        return;
      }
      bytes_read += result->size();
      if (bytes_read < buffer.size()) {
        run();
      } else {
        done(buffer);
      }
    }
  };
  reader{this, buffer, std::move(done)}.run();
}

void stream::write_some(
    span<const char> buffer,
    std::function<void(result<span<const char>>)> done) noexcept {
  auto& state = socket_.state();
  auto handler = [&state, buffer, done] {
    int result = ::write((int)state.handle, buffer.data(), buffer.size());
    if (result != -1) {
      done(buffer.subspan(result));
    } else {
      done(error{posix_status(errno)});
    }
  };
  if (status s = socket_.context().await_out(state, std::move(handler));
      !s.success()) {
    done(error{std::move(s)});
  }
}

void stream::write(span<const char> buffer,
                   std::function<void(status)> done) noexcept {
  struct writer {
    stream* output;
    span<const char> remaining;
    std::function<void(status)> done;
    void run() {
      output->write_some(remaining, *this);
    }
    void operator()(result<span<const char>> result) {
      if (result.failure()) {
        done(std::move(result).status());
        return;
      }
      remaining = *result;
      if (!remaining.empty()) {
        run();
      } else {
        done(status_code::ok);
      }
    }
  };
  writer{this, buffer, std::move(done)}.run();
}

io_context& stream::context() const noexcept { return socket_.context(); }

acceptor::acceptor(socket socket) noexcept : socket_(std::move(socket)) {}

void acceptor::accept(std::function<void(result<stream>)> done) noexcept {
  auto& state = socket_.state();
  auto handler = [&context = socket_.context(), &state, done] {
    int handle = ::accept4((int)state.handle, nullptr, nullptr, SOCK_NONBLOCK);
    if (handle == -1) {
      done(error{posix_status(errno)});
      return;
    }
    result<socket> s =
        socket::create(context, unique_handle{file_handle{handle}});
    if (s.success()) {
      done(std::move(*s));
    } else {
      done(error{std::move(s).status()});
    }
  };
  if (status s = socket_.context().await_in(state, std::move(handler));
      !s.success()) {
    done(error{std::move(s)});
  }
}

io_context& acceptor::context() const noexcept { return socket_.context(); }

result<acceptor> bind(io_context& context, const address& address) {
  result<addr_info> info = addr_info::resolve(
      address.host.c_str(), std::to_string(address.port).c_str());
  if (info.failure()) return error{std::move(info).status()};
  result<socket> socket =
      socket::create(context, unique_handle{file_handle{::socket(
                                  info->get()->ai_family, SOCK_STREAM, 0)}});
  if (socket.failure()) return error{std::move(socket).status()};
  if (status s = set_non_blocking(*socket); s.failure()) {
    return error{std::move(s)};
  }
  const int bind_result = ::bind((int)socket->handle(), info->get()->ai_addr,
                                 info->get()->ai_addrlen);
  if (bind_result == -1) return posix_error(errno);
  const int listen_result =
      ::listen((int)socket->handle(), acceptor::max_pending_connections);
  if (listen_result == -1) return posix_error(errno);
  return acceptor{std::move(*socket)};
}

result<stream> connect(io_context& context, const address& address) {
  result<addr_info> info = addr_info::resolve(
      address.host.c_str(), std::to_string(address.port).c_str());
  if (info.failure()) return error{std::move(info).status()};
  result<socket> socket =
      socket::create(context, unique_handle{file_handle{::socket(
                                  info->get()->ai_family, SOCK_STREAM, 0)}});
  if (socket.failure()) return error{std::move(socket).status()};
  if (status s = set_non_blocking(*socket); s.failure()) {
    return error{std::move(s)};
  }
  const int connect_result = ::connect(
      (int)socket->handle(), info->get()->ai_addr, info->get()->ai_addrlen);
  if (connect_result == -1) return posix_error(errno);
  return stream{std::move(*socket)};
}

}  // namespace util
