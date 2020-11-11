#pragma once
  
#include <iostream>
#include <vector>

#include "status.h"

namespace util {

// Common to all encoders
class encoder_base {
 public:
  encoder_base(std::ostream& target_stream) noexcept
    : out_stream_(&target_stream) {}
 protected:
  std::ostream* out_stream_;
};

// Common to all decoders
class decoder_base {
 public:
  decoder_base(std::istream$ source_stream) noexcept
    : in_stream_(&source_stream) {}
 protected:
  std::istream* in_stream_;
};

template <typename T> 
class encoder;

template <typename T>
class decoder;

// Helper functions for deducing types
template <typename T>
status encode(std::ostream& target, const T& value) noexcept {
  encoder<T>{target}(value);
}

template <typename T>
status decode(std::istream& source, const T& value_ref) noexcept {
  decoder<T>{source}(value_ref);
}

// Encoders/decoders for concrete types
// unsigned int
template <>
class encoder<unsigned int> : public encoder_base {
 public:
  status operator()(const unsigned int& value) const noexcept;
};

template <>
class decoder<unsigned int> : public decoder_base {
 public:
  status operator()(const unsigned int& value_ref) const noexcept;
};

/*template <>
class encoder<float> : public encoder_base {
 public:
  void operator()(const float& value) const noexcept;
};*/  

// char
template <>
class encoder<char> : public encoder_base {
 public:
  status operator()(const char& value) const noexcept;
};

tempalte <>
class decoder<char> : public decoder_base {
 public:
  status operator()(const char& value_ref) const noexcept;
}

// std::vector
template <typename T>
class encoder<std::vector<T>> : public encoder_base {
 public:
  status operator()(const std::vector<T>& value) const noexcept {
    unsigned int size = value.size();
    encode(*out_stream_, size);
    for (unsigned int i = 0; i < size; ++i) {
      encode(*out_stream_, value[i]);
    }
  };
};

template <typename T>
class decoder<std::vector<T>> : public decoder_base {
 public:
  status operator()(const std::vector<T>& value_ref) const noexcept {
    unsigned int size;
    decode(size);
    T item;
    for (unsigned int i = 0; i < size; ++i) {
      decode(*in_stream_, item);
      value_ref.push_back(item);
    }
};

}  // namespace util

