#pragma once

#include <cassert>
#include <iostream>
#include <string_view>

namespace util {

// All status objects can decay to a canonical code from this selection.
enum class status_code : int {
  ok,               // success!
  client_error,     // client error, e.g. misuse of an API.
  transient_error,  // temporary failure, client may retry.
  permanent_error,  // permanant failure, client should not retry.
  not_available,    // requested functionality is not available.
  unknown_error,    // an unknown error condition.
};

// A status stores a payload and an unowned pointer to a manager. The manager
// encodes the semantics of the payload.
union status_payload {
  void* pointer;
  int code;
};

struct status_manager {
  // Returns the unique ID of the domain. This should be a randomly chosen
  // value, which has a very low probability of conflicts. Note that multiple
  // status manager instances may share the same domain id if they have the same
  // interpretation of code values.
  virtual std::uint64_t domain_id() const noexcept = 0;
  // Returns the name of the status domain.
  virtual std::string_view domain() const noexcept = 0;
  // Returns the name for a raw status code.
  virtual std::string_view name(status_payload payload) const noexcept = 0;
  // Returns true if the given code represents a failure in this domain.
  virtual bool failure(status_payload payload) const noexcept = 0;
  // Returns a canonical code derived from the given domain code.
  virtual status_code canonical(status_payload payload) const noexcept = 0;
  // Returns the raw status code for a payload.
  virtual int code(status_payload payload) const noexcept = 0;
  // Append additional information to the textual representation of a status. By
  // default, a status will be of the form "domain::name", but if the payload
  // has additional context it may append it.
  virtual void output(std::ostream& output,
                      status_payload payload) const noexcept = 0;
  // Destroys a payload object. For trivial status objects this is a no-op, but
  // it can be used to clean up owned resources for more complex status types.
  virtual void destroy(status_payload payload) const noexcept = 0;

  friend bool operator==(const status_manager& l,
                         const status_manager& r) noexcept {
    return l.domain_id() == r.domain_id();
  }
};

template <typename T, typename = void>
struct can_make_status {
  static constexpr bool value = false;
};

template <typename T>
struct can_make_status<T,
                       std::void_t<decltype(make_status(std::declval<T>()))>> {
  static constexpr bool value = true;
};

template <typename T, typename = void>
struct can_make_status_with_message {
  static constexpr bool value = false;
};

template <typename T>
struct can_make_status_with_message<
    T, std::void_t<decltype(
           make_status(std::declval<T>(), std::declval<std::string>()))>> {
  static constexpr bool value = true;
};

class [[nodiscard]] status {
 public:
  status() noexcept;
  status(const status_manager& manager, status_payload payload) noexcept;

  // A status can be implicitly constructed from any type T for which
  // make_status(T) exists.
  template <typename T, typename = std::enable_if_t<can_make_status<T>::value>>
  status(T&& code) noexcept : status(make_status(std::forward<T>(code))) {}

  // A status with a message can be implicitly constructed from any type T for
  // which make_status(T, std::string) exists.
  template <typename T,
            typename = std::enable_if_t<can_make_status_with_message<T>::value>>
  status(T&& code, std::string message) noexcept
      : status(make_status(std::forward<T>(code), std::move(message))) {}

  ~status() noexcept;

  // Not copyable.
  status(const status&) = delete;
  status& operator=(const status&) = delete;

  // Movable.
  status(status&& other) noexcept;
  status& operator=(status&& other) noexcept;

  // Check the status for success or failure. success() == !failure().
  bool success() const noexcept;
  bool failure() const noexcept;

  // Returns the domain of the status. These can be compared with == to
  // determine if two statuses have the same domain.
  const status_manager& domain() const noexcept;

  // Returns the raw code of this status object. This generally cannot be
  // interpreted without checking the domain of the status.
  int code() const noexcept;

  // Translates this status into a canonical status.
  status canonical() const noexcept;

  friend std::ostream& operator<<(std::ostream& output, const status& s);

  // Compare two status objects for equality. If the two statuses have the same
  // domain, the comparison is exact. Otherwise, the comparison decays to
  // comparing the canonical codes.
  friend bool operator==(const status& l, const status& r);

 private:
  friend class error;

  status_payload payload_;
  const status_manager* manager_;
};

status make_status(status_code) noexcept;
status make_status(status_code, std::string) noexcept;

// An error is a status that always represents a failure. Success values should
// never be assigned to error objects: in debug builds, this will cause an
// assertion failure, while in production builds the success will be replaced
// with a status_code::unknown_error.
class [[nodiscard]] error : public status {
 public:
  explicit error(status) noexcept;
  error& operator=(status&&) noexcept;
  explicit error(std::exception_ptr) noexcept;
  bool success() const noexcept;
  bool failure() const noexcept;
};

struct posix_code {
  posix_code(int code) noexcept;
  posix_code(std::errc code) noexcept;
  int value;
};

status make_status(posix_code) noexcept;
status make_status(posix_code, std::string message) noexcept;

// Helper functions for constructing errors with messages.
error client_error(std::string message) noexcept;
error transient_error(std::string message) noexcept;
error permanent_error(std::string message) noexcept;
error not_available(std::string message) noexcept;
error unknown_error(std::string message) noexcept;

}  // namespace util
