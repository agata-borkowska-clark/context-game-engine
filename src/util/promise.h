#pragma once

#include <coroutine>
#include <cstdlib>
#include <utility>

namespace util {

void promise_exception();

template <typename Promise, typename T>
struct promise_storage {
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
      std::coroutine_handle<Promise>::from_promise(*this).destroy();
    } else {
      state = state_type::resolved;
    }
  }
  state_type state;
  union { T value; };
};

template <typename Promise, typename T>
struct promise_type : promise_storage<Promise, T> {
  using base = promise_storage<Promise, T>;
  Promise get_return_object() noexcept { return Promise(*this); }
  template <typename U = T>
  requires std::is_constructible_v<T, U>
  void return_value(U&& x) noexcept(std::is_nothrow_constructible_v<T, U>) {
    assert(base::state != base::state_type::resolved);
    new(&base::value) T(std::forward<U>(x));
    base::state = base::state_type::resolved;
  }
  T consume() noexcept {
    assert(base::state == base::state_type::resolved);
    return std::move(base::value);
  }
};

template <typename Promise>
struct promise_type<Promise, void> : promise_storage<Promise, char> {
  using base = promise_storage<Promise, char>;
  void return_void() noexcept {
    base::state = base::state_type::resolved;
  }
  void consume() const noexcept {}
};

template <typename T>
class promise {
 public:
  using promise_type = ::util::promise_type<promise, T>;
  using handle = std::coroutine_handle<promise_type>;

  promise() noexcept = default;
  ~promise() {
    if (!value_) return;
    if (value_->state == promise_type::state_type::resolved) {
      handle::from_promise(value_)->destroy();
    } else {
      value_->state = promise_type::state_type::detached;
    }
  }

  // Non-copyable.
  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  // Movable.
  promise(promise&& other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  promise& operator=(promise&& other) noexcept {
    if (value_) handle::from_promise(value_).destroy();
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
  explicit promise(promise_type* value) noexcept : value_(value) {}
  promise_type* value_ = nullptr;
};

}  // namespace
