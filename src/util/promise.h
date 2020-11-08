#pragma once

#include <cassert>
#include <coroutine>
#include <new>
#include <utility>

namespace util {

template <typename T>
struct promise_storage {
  promise_storage() noexcept {}
  ~promise_storage() noexcept {
    if (state == state_type::ready) value.~T();
  }

  // Not copyable.
  promise_storage(const promise_storage&) = delete;
  promise_storage& operator=(const promise_storage&) = delete;

  // Not movable.
  promise_storage(promise_storage&&) = delete;
  promise_storage& operator=(promise_storage&&) = delete;

  enum class state_type {
    empty,    // No result is ready and no awaiter is present.
    waiting,  // Awaiting a result that is yet to be provided.
    ready,    // Storing a value which is yet to be awaited.
  };

  bool ready() const noexcept { return state == state_type::ready; }

  template <typename... Args>
  requires std::is_constructible_v<T, Args...>
  void resolve(Args&&... args) noexcept {
    assert(state != state_type::ready);
    auto w = state == state_type::waiting ? waiter : std::coroutine_handle<>();
    new(&value) T(std::forward<Args>(args)...);
    state = state_type::ready;
    if (w) w.resume();
  }

  void wait(std::coroutine_handle<> handle) noexcept {
    assert(state == state_type::empty);
    waiter = handle;
    state = state_type::waiting;
  }

  T consume() noexcept {
    assert(state == state_type::ready);
    return std::move(value);
  }

  state_type state = state_type::empty;
  union {
    std::coroutine_handle<> waiter;
    T value;
  };
};

template <>
struct promise_storage<void> : promise_storage<bool> {
  void resolve() noexcept { promise_storage<bool>::resolve(true); }
  void consume() noexcept {}
};

template <typename T>
class [[nodiscard]] promise : promise_storage<T> {
 public:
  promise() = default;

  template <typename F>
  explicit promise(F&& f) { std::forward<F>(f)(*this); }

  bool await_ready() const noexcept { return this->ready(); }
  T await_resume() noexcept { return std::move(this->consume()); }
  void await_suspend(std::coroutine_handle<> handle) noexcept {
    this->wait(handle);
  }

  using promise_storage<T>::resolve;
};

}  // namespace util
