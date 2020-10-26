#pragma once

#include "status.h"

namespace util {

template <typename Base>
struct code_manager final : public Base {
  template <typename T>
  status make(T code) const noexcept {
    status_payload payload;
    payload.code = (int)code;
    return status(*this, payload);
  }
  constexpr int code(status_payload payload) const noexcept final {
    return payload.code;
  }
  void output(std::ostream&, status_payload) const noexcept final {}
  void destroy(status_payload) const noexcept final {}
};

struct code_with_message_payload {
  int code;
  std::string message;
};

template <typename Base>
struct code_with_message_manager : public Base {
  template <typename T>
  status make(T code, std::string message) const noexcept {
    status_payload payload;
    payload.pointer =
        new code_with_message_payload{(int)code, std::move(message)};
    return status(*this, payload);
  }
  constexpr code_with_message_payload* payload(
      status_payload p) const noexcept {
    return static_cast<code_with_message_payload*>(p.pointer);
  }
  constexpr int code(status_payload p) const noexcept final {
    return payload(p)->code;
  }
  void output(std::ostream& output, status_payload p) const noexcept final {
    output << ": " << payload(p)->message;
  }
  void destroy(status_payload p) const noexcept final {
    delete payload(p);
  }
};

}  // namespace util
