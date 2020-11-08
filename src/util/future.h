#pragma once

#include "promise.h"

#include <cassert>
#include <coroutine>
#include <cstdlib>
#include <utility>

namespace util {

struct future_promise_base {
  enum class state_type {
    unresolved,  // unfinished business, and the promise is still attached.
    resolved,    // computation is complete, promise is still attached.
    detached,    // promise is detached but computation is outstanding.
  };

  std::suspend_never initial_suspend() const noexcept { return {}; }

  auto final_suspend() noexcept {
    assert(state != state_type::resolved);
    struct suspend_sometimes {
      bool suspend;
      bool await_ready() const noexcept { return !suspend; }
      void await_resume() {}
      void await_suspend(std::coroutine_handle<>) noexcept {}
    };
    const bool suspend = state == state_type::unresolved;
    state = state_type::resolved;
    return suspend_sometimes{suspend};
  }

  void unhandled_exception() const noexcept { std::abort(); }

  state_type state = state_type::unresolved;
};

template <typename Future, typename T>
struct future_promise : future_promise_base {
  Future get_return_object() noexcept { return Future(this); }

  template <typename... Args>
  requires std::is_constructible_v<T, Args...>
  void return_value(Args&&... args) noexcept {
    storage.resolve(std::forward<Args>(args)...);
  }

  promise_storage<T> storage;
};

template <typename Future>
struct future_promise<Future, void> : future_promise_base {
  Future get_return_object() noexcept { return Future(this); }

  void return_void() noexcept { storage.resolve(); }

  promise_storage<void> storage;
};

template <typename T>
class [[nodiscard]] future {
 public:
  using promise_type = future_promise<future<T>, T>;
  using handle = std::coroutine_handle<promise_type>;

  future() noexcept = default;
  ~future() {
    if (!value_) return;
    if (value_->state == promise_type::state_type::resolved) {
      handle::from_promise(*value_).destroy();
    } else {
      value_->state = promise_type::state_type::detached;
    }
  }

  // Non-copyable.
  future(const future&) = delete;
  future& operator=(const future&) = delete;

  // Movable.
  future(future&& other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  future& operator=(future&& other) noexcept {
    if (value_) handle::from_promise(*value_).destroy();
    value_ = std::exchange(other.value_, nullptr);
    return *this;
  }

  // Await support.
  bool await_ready() const noexcept {
    return value_->storage.ready();
  }
  auto await_resume() noexcept {
    return value_->storage.consume();
  }
  void await_suspend(std::coroutine_handle<> handle) noexcept {
    value_->storage.wait(handle);
  }

 private:
  friend promise_type;
  explicit future(promise_type* value) noexcept : value_(value) {}
  promise_type* value_ = nullptr;
};

}  // namespace
