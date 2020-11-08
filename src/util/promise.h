#pragma once

#include <cassert>
#include <coroutine>
#include <utility>

namespace util {

template <typename T>
class promise {
 public:
  promise() = default;

  template <typename F>
  explicit promise(F&& f) { std::forward<F>(f)(*this); }

  ~promise() noexcept {
    if (state_ == state_type::ready) value_.~T();
  }

  // Non-copyable.
  promise(const promise&) = delete;
  promise& operator=(const promise&) = delete;

  // Non-movable.
  promise(promise&&) = delete;
  promise& operator=(promise&&) = delete;

  bool await_ready() const noexcept { return state_ == state_type::ready; }
  T await_resume() noexcept {
    return std::move(value_);
  }
  void await_suspend(std::coroutine_handle<> handle) noexcept {
    assert(state_ == state_type::empty);
    waiter_ = std::coroutine_handle<>(handle);
    state_ = state_type::waiting;
  }

  template <typename... Args>
  requires std::is_constructible_v<T, Args...>
  void resolve(Args&&... args)
      noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    assert(state_ != state_type::ready);
    if (state_ == state_type::waiting) {
      auto handle = waiter_;
      new(&value_) T(std::forward<Args>(args)...);
      handle();
    } else {
      new(&value_) T(std::forward<Args>(args)...);
      state_ = state_type::ready;
    }
  }

 private:
  enum class state_type {
    empty,    // No result is ready and no awaiter is present.
    waiting,  // Awaiting a result that is yet to be provided.
    ready,    // Storing a value which is yet to be awaited.
  };
  union {
    std::coroutine_handle<> waiter_;
    T value_;
  };
  state_type state_ = state_type::empty;
};

}  // namespace util
