#pragma once
  
#include <iostream>
#include <vector>

namespace util {

// Common to all encoders
class encoder_base {
 public:
    encoder_base(std::ostream& target_stream) noexcept
      : out_stream_(&target stream) {}
 protected:
    std::ostream* out_stream_;
}

template <typename T> 
class encoder;

// Helper function for deducing types
template <typename T>
void encode(std::ostream& target, const T& value) noexcept {
  encoder<T>{target}(value);
}

// Decoders for concrete types
template <>
class encoder<int> : public encoder_base {
 public:
  void operator()(const int& value) const noexcept;
};

template <>
class encoder<float> : public encoder_base {
 public:
  void operator()(const float& value) const noexcept;
};  

template <>
class encoder<char> : public encoder_base {
 public:
  void operator()(const char& value) const noexcept;
}

template <typename T>
class encoder<std::vector<T>> : public encoder_base {
 public:
  void operator()(const std::vector<T>& value) const noexcept {
    unsigned int size = value.size();
    encode(*out_stream_, size);
    for (unsigned int i = 0; i < size; ++i) {
      encode(*out_stream_, value[i]);
    }
  };
}

}  // namespace util

