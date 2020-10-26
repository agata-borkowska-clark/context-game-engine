#include "status.h"

#include "status_managers.h"

namespace util {

// Base status manager for canonical codes.
struct status_code_manager_base : status_manager {
  constexpr std::uint64_t domain_id() const noexcept final {
    // Randomly chosen bytes.
    return 0x3f'f4'c5'8c'78'c1'60'89;
  }
  constexpr std::string_view domain() const noexcept final {
    return "status_code";
  }
  std::string_view name(status_payload payload) const noexcept final {
    switch (status_code{code(payload)}) {
      case status_code::ok: return "ok";
      case status_code::client_error: return "client_error";
      case status_code::transient_error: return "transient_error";
      case status_code::permanent_error: return "permanent_error";
      case status_code::not_available: return "not_available";
      case status_code::unknown_error: return "unknown_error";
    }
    return "<invalid>";
  }
  bool failure(status_payload payload) const noexcept final {
    return status_code{code(payload)} != status_code::ok;
  }
  status_code canonical(status_payload payload) const noexcept final {
    return status_code{code(payload)};
  }
};

// Status managers for canonical codes.
static constexpr code_manager<status_code_manager_base> status_code_manager;
static constexpr code_with_message_manager<status_code_manager_base>
    payload_status_manager;

struct exception_payload {
  const std::type_info& type;
  std::string message;
};

// Status manager for status objects derived from an exception.
struct exception_manager final : status_manager {
  constexpr exception_payload* payload(status_payload p) const noexcept {
    return (exception_payload*)p.pointer;
  }
  constexpr std::uint64_t domain_id() const noexcept final {
    return 0x91'36'52'8a'de'ea'cb'5d;
  }
  constexpr std::string_view domain() const noexcept final {
    return "exception";
  }
  std::string_view name(status_payload p) const noexcept final {
    return payload(p)->type.name();
  }
  constexpr bool failure(status_payload) const noexcept final { return true; }
  constexpr status_code canonical(status_payload) const noexcept final {
    return status_code::unknown_error;
  }
  constexpr int code(status_payload) const noexcept final {
    return (int)status_code::unknown_error;
  }
  void output(std::ostream& output, status_payload p) const noexcept final {
    const std::string& message = payload(p)->message;
    if (!message.empty()) {
      output << ": " << message;
    }
  }
  constexpr void destroy(status_payload p) const noexcept final {
    delete payload(p);
  }
};
static constexpr exception_manager exception_manager;

// Base status manager for canonical codes.
struct posix_manager_base : status_manager {
  constexpr std::uint64_t domain_id() const noexcept final {
    // Randomly chosen bytes.
    return 0x58'8f'91'e8'63'06'9f'e5;
  }
  constexpr std::string_view domain() const noexcept final {
    return "posix";
  }
  std::string_view name(status_payload payload) const noexcept final {
    // TODO: Add mappings for useful posix codes.
    switch (std::errc{code(payload)}) {
      case std::errc{}: return "ok";
    }
#ifndef NDEBUG
    std::cerr << "Unhandled posix code: " << code(payload) << '\n';
    std::abort();
#endif
    return "<unknown>";
  }
  bool failure(status_payload payload) const noexcept final {
    return std::errc{code(payload)} != std::errc{};
  }
  status_code canonical(status_payload payload) const noexcept final {
    switch (std::errc{code(payload)}) {
      case std::errc{}: return status_code::ok;
    };
    return status_code::unknown_error;
  }
};

// Status managers for posix codes.
static constexpr code_manager<posix_manager_base> posix_manager;
static constexpr code_with_message_manager<posix_manager_base>
    posix_payload_manager;

status::status() noexcept : status(status_code::ok) {}

status::status(status_code code) noexcept {
  payload_.code = (int)code;
  manager_ = &status_code_manager;
}

status::status(const status_manager& manager, status_payload payload) noexcept
    : payload_(payload), manager_(&manager) {}

status::status(status_code code, std::string message) {
  payload_.pointer =
      new code_with_message_payload{(int)code, std::move(message)};
  manager_ = &payload_status_manager;
}

status::~status() noexcept { manager_->destroy(payload_); }

status::status(status&& other) noexcept
    : payload_(other.payload_),
      manager_(other.manager_) {
  other.payload_.code = (int)manager_->canonical(payload_);
  other.manager_ = &status_code_manager;
}

status& status::operator=(status&& other) noexcept {
  if (this == &other) return *this;
  manager_->destroy(payload_);
  payload_ = other.payload_;
  manager_ = other.manager_;
  other.payload_.code = (int)manager_->canonical(payload_);
  other.manager_ = &status_code_manager;
  return *this;
}

bool status::success() const noexcept { return !manager_->failure(payload_); }
bool status::failure() const noexcept { return manager_->failure(payload_); }
const status_manager& status::domain() const noexcept { return *manager_; }
int status::code() const noexcept { return manager_->code(payload_); }
status status::canonical() const noexcept {
  return manager_->canonical(payload_);
}

std::ostream& operator<<(std::ostream& output, const status& s) {
  output << s.manager_->domain() << "::" << s.manager_->name(s.payload_);
  s.manager_->output(output, s.payload_);
  return output;
}

bool operator==(const status& l, const status& r) {
  if (*l.manager_ == *r.manager_) {
    return l.code() == r.code();
  } else {
    return l.canonical() == r.canonical();
  }
}

static status expect_error(status s) {
  assert(s.failure());
  return s.failure() ? std::move(s) : status_code::unknown_error;
}

error::error(status s) noexcept : status(expect_error(std::move(s))) {}

error& error::operator=(status&& s) noexcept {
  status::operator=(expect_error(std::move(s)));
  return *this;
}

error::error(std::exception_ptr p) noexcept {
  try {
    std::rethrow_exception(p);
  } catch (const std::exception& e) {
    payload_.pointer = new exception_payload{
      .type = typeid(e),
      .message = e.what(),
    };
    manager_ = &exception_manager;
  } catch (...) {
    *this = status_code::unknown_error;
  }
}

bool error::success() const noexcept {
  assert(!status::success());
  return false;
}

bool error::failure() const noexcept {
  assert(status::failure());
  return true;
}

error client_error(std::string message) noexcept {
  return error{status(status_code::client_error, std::move(message))};
}

error transient_error(std::string message) noexcept {
  return error{status(status_code::transient_error, std::move(message))};
}

error permanent_error(std::string message) noexcept {
  return error{status(status_code::permanent_error, std::move(message))};
}

error not_available(std::string message) noexcept {
  return error{status(status_code::not_available, std::move(message))};
}

error unknown_error(std::string message) noexcept {
  return error{status(status_code::unknown_error, std::move(message))};
}

status posix_status(int code) noexcept {
  status_payload payload;
  payload.code = code;
  return status(posix_manager, payload);
}

status posix_status(int code, std::string message) noexcept {
  status_payload payload;
  payload.pointer = new code_with_message_payload{code, std::move(message)};
  return status(posix_payload_manager, payload);
}

error posix_error(int code) noexcept { return error{posix_status(code)}; }

error posix_error(int code, std::string message) noexcept {
  return error{posix_status(code, std::move(message))};
}

}  // namespace util
