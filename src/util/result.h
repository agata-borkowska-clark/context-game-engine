#pragma once

#include "status.h"

#include <type_traits>

namespace util {

// A result<T> represents either a T or an error.
template <typename T>
class result {
 public:
  // Construct a successful result from any set of arguments that can be
  // converted to a T.
  template <typename... Args,
            typename = std::enable_if_t<std::is_constructible_v<T, Args...>>>
  constexpr result(Args&&... args)
      noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    new(&value_) T(std::forward<Args>(args)...);
  }

  // Construct a failure result.
  constexpr result(error e) noexcept : status_(std::move(e)) {}

  ~result() noexcept {
    if (success()) value_.~T();
  }

  // Not copyable.
  result(const result&) = delete;
  result& operator=(const result&) = delete;

  // Movable.
  constexpr result(result&& other) noexcept
      : status_(std::move(other.status_)) {
    if (success()) new(&value_) T(std::move(other.value_));
  }

  constexpr result& operator=(result&& other) noexcept {
    if (success()) value_.~T();
    status_ = std::move(other.status_);
    if (success()) new(&value_) T(std::move(other.value_));
    return *this;
  }

  constexpr bool success() const noexcept { return status_.success(); }
  constexpr bool failure() const noexcept { return status_.failure(); }

  // Access the status of the result. The result stores a T if and only if the
  // status represents success. A move accessor is available to allow
  // consumption of the status. It is undefined behaviour to use this accessor
  // to replace the status with one that has a different value for failure().
  constexpr const util::status& status() const& noexcept { return status_; }
  constexpr util::status&& status() && noexcept { return std::move(status_); }

  // Accessors for the stored T. Requires success().

  constexpr T& operator*() & noexcept {
    assert(success());
    return value_;
  }
  constexpr const T& operator*() const& noexcept {
    assert(success());
    return value_;
  }
  constexpr T&& operator*() && noexcept {
    assert(success());
    return std::move(value_);
  }
  constexpr const T&& operator*() const&& noexcept {
    assert(success());
    return std::move(value_);
  }
  constexpr T* operator->() & noexcept {
    assert(success());
    return &value_;
  }
  constexpr const T* operator->() const& noexcept {
    assert(success());
    return &value_;
  }

 private:
  util::status status_;
  union { T value_; };
};

}  // namespace util
