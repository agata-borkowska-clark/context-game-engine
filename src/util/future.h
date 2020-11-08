#pragma once

#include <cassert>
#include <coroutine>
#include <cstdlib>
#include <utility>

namespace util {

void promise_exception();

template <typename Future, typename T>
struct promise_storage {
  promise_storage() noexcept {}
  ~promise_storage() noexcept {
    if (state == state_type::resolved) value.~T();
  }
  enum class state_type {
    unresolved,  // unfinished business, and the promise is still attached.
    resolved,    // computation is complete, promise is still attached.
    detached,    // promise is detached but computation is outstanding.
  };
  std::suspend_never initial_suspend() const noexcept { return {}; }
  std::suspend_always final_suspend() const noexcept { return {}; }
  void unhandled_exception() const noexcept { std::abort(); }
  void resolve() noexcept {
    if (state == state_type::detached) {
      std::coroutine_handle<Future>::from_promise(*this).destroy();
    } else {
      state = state_type::resolved;
    }
  }
  state_type state = state_type::unresolved;
  union { T value; };
};

template <typename Future, typename T>
struct promise_type : promise_storage<Future, T> {
  using base = promise_storage<Future, T>;
  Future get_return_object() noexcept { return Future(this); }
  template <typename U = T>
  requires std::is_constructible_v<T, U>
  void return_value(U&& x) noexcept(std::is_nothrow_constructible_v<T, U>) {
    assert(this->state != base::state_type::resolved);
    new(&this->value) T(std::forward<U>(x));
    this->state = base::state_type::resolved;
  }
  T consume() noexcept {
    assert(this->state == base::state_type::resolved);
    return std::move(this->value);
  }
};

template <typename Future>
struct promise_type<Future, void> : promise_storage<Future, char> {
  using base = promise_storage<Future, char>;
  void return_void() noexcept {
    base::state = base::state_type::resolved;
  }
  void consume() const noexcept {}
};

template <typename T>
class future {
 public:
  using promise_type = ::util::promise_type<future<T>, T>;
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
    return value_->state == promise_type::state_type::resolved;
  }
  auto await_resume() noexcept {
    assert(await_ready());
    return value_->consume();
  }
  void await_suspend(std::coroutine_handle<> handle) noexcept {
    assert(!await_ready());
    value_->reader = std::move(handle);
  }

 private:
  friend promise_type;
  explicit future(promise_type* value) noexcept : value_(value) {}
  promise_type* value_ = nullptr;
};

}  // namespace
