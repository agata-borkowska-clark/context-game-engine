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
      case std::errc{}:
        return "ok";
      case std::errc::address_family_not_supported:
        return "address_family_not_supported";
      case std::errc::address_in_use:
        return "address_in_use";
      case std::errc::address_not_available:
        return "address_not_available";
      case std::errc::already_connected:
        return "already_connected";
      case std::errc::argument_list_too_long:
        return "argument_list_too_long";
      case std::errc::argument_out_of_domain:
        return "argument_out_of_domain";
      case std::errc::bad_address:
        return "bad_address";
      case std::errc::bad_file_descriptor:
        return "bad_file_descriptor";
      case std::errc::bad_message:
        return "bad_message";
      case std::errc::broken_pipe:
        return "broken_pipe";
      case std::errc::connection_aborted:
        return "connection_aborted";
      case std::errc::connection_already_in_progress:
        return "connection_already_in_progress";
      case std::errc::connection_refused:
        return "connection_refused";
      case std::errc::connection_reset:
        return "connection_reset";
      case std::errc::cross_device_link:
        return "cross_device_link";
      case std::errc::destination_address_required:
        return "destination_address_required";
      case std::errc::device_or_resource_busy:
        return "device_or_resource_busy";
      case std::errc::directory_not_empty:
        return "directory_not_empty";
      case std::errc::executable_format_error:
        return "executable_format_error";
      case std::errc::file_exists:
        return "file_exists";
      case std::errc::file_too_large:
        return "file_too_large";
      case std::errc::filename_too_long:
        return "filename_too_long";
      case std::errc::function_not_supported:
        return "function_not_supported";
      case std::errc::host_unreachable:
        return "host_unreachable";
      case std::errc::identifier_removed:
        return "identifier_removed";
      case std::errc::illegal_byte_sequence:
        return "illegal_byte_sequence";
      case std::errc::inappropriate_io_control_operation:
        return "inappropriate_io_control_operation";
      case std::errc::interrupted:
        return "interrupted";
      case std::errc::invalid_argument:
        return "invalid_argument";
      case std::errc::invalid_seek:
        return "invalid_seek";
      case std::errc::io_error:
        return "io_error";
      case std::errc::is_a_directory:
        return "is_a_directory";
      case std::errc::message_size:
        return "message_size";
      case std::errc::network_down:
        return "network_down";
      case std::errc::network_reset:
        return "network_reset";
      case std::errc::network_unreachable:
        return "network_unreachable";
      case std::errc::no_buffer_space:
        return "no_buffer_space";
      case std::errc::no_child_process:
        return "no_child_process";
      case std::errc::no_link:
        return "no_link";
      case std::errc::no_lock_available:
        return "no_lock_available";
      case std::errc::no_message_available:
        return "no_message_available";
      case std::errc::no_message:
        return "no_message";
      case std::errc::no_protocol_option:
        return "no_protocol_option";
      case std::errc::no_space_on_device:
        return "no_space_on_device";
      case std::errc::no_stream_resources:
        return "no_stream_resources";
      case std::errc::no_such_device_or_address:
        return "no_such_device_or_address";
      case std::errc::no_such_device:
        return "no_such_device";
      case std::errc::no_such_file_or_directory:
        return "no_such_file_or_directory";
      case std::errc::no_such_process:
        return "no_such_process";
      case std::errc::not_a_directory:
        return "not_a_directory";
      case std::errc::not_a_socket:
        return "not_a_socket";
      case std::errc::not_a_stream:
        return "not_a_stream";
      case std::errc::not_connected:
        return "not_connected";
      case std::errc::not_enough_memory:
        return "not_enough_memory";
      case std::errc::not_supported:
        return "not_supported";
      case std::errc::operation_canceled:
        return "operation_canceled";
      case std::errc::operation_in_progress:
        return "operation_in_progress";
      case std::errc::operation_not_permitted:
        return "operation_not_permitted";
      case std::errc::owner_dead:
        return "owner_dead";
      case std::errc::permission_denied:
        return "permission_denied";
      case std::errc::protocol_error:
        return "protocol_error";
      case std::errc::protocol_not_supported:
        return "protocol_not_supported";
      case std::errc::read_only_file_system:
        return "read_only_file_system";
      case std::errc::resource_deadlock_would_occur:
        return "resource_deadlock_would_occur";
      case std::errc::resource_unavailable_try_again:
        return "resource_unavailable_try_again";
      case std::errc::result_out_of_range:
        return "result_out_of_range";
      case std::errc::state_not_recoverable:
        return "state_not_recoverable";
      case std::errc::stream_timeout:
        return "stream_timeout";
      case std::errc::text_file_busy:
        return "text_file_busy";
      case std::errc::timed_out:
        return "timed_out";
      case std::errc::too_many_files_open_in_system:
        return "too_many_files_open_in_system";
      case std::errc::too_many_files_open:
        return "too_many_files_open";
      case std::errc::too_many_links:
        return "too_many_links";
      case std::errc::too_many_symbolic_link_levels:
        return "too_many_symbolic_link_levels";
      case std::errc::value_too_large:
        return "value_too_large";
      case std::errc::wrong_protocol_type:
        return "wrong_protocol_type";
    }
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

status::status(const status_manager& manager, status_payload payload) noexcept
    : payload_(payload), manager_(&manager) {}

status::status(std::errc code) noexcept
    : status(posix_manager.make(code)) {}

status::status(std::errc code, std::string message) noexcept
    : status(posix_payload_manager.make(code, std::move(message))) {}

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

bool operator==(const status& l, const status& r) noexcept {
  if (*l.manager_ == *r.manager_) {
    return l.code() == r.code();
  } else {
    return l.canonical() == r.canonical();
  }
}

bool operator!=(const status& l, const status& r) noexcept { return !(l == r); }

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

status make_status(status_code code) noexcept {
  return status_code_manager.make(code);
}

status make_status(status_code code, std::string message) noexcept {
  return payload_status_manager.make(code, std::move(message));
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

}  // namespace util
