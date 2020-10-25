#pragma once

namespace util {
namespace detail {

template <typename... Ts>
struct void_t_impl {
  using type = void;
};
template <typename... Ts>
using void_t = typename void_t_impl<Ts...>::type;

template <typename Container, typename T, typename = void>
struct span_like {
  static constexpr bool value = false;
};

template <typename Container, typename T>
struct span_like<Container, T,
                 void_t<decltype(std::declval<Container>().data()),
                        decltype(std::declval<Container>().size())>> {
  using element_type =
      std::decay_t<decltype(std::declval<Container>().data()[0])>;
  static constexpr bool value =
      std::is_convertible_v<element_type (&)[], T (&)[]>;
};

template <typename Container, typename T>
static constexpr bool span_like_v = span_like<Container, T>::value;

}  // namespace detail

template <typename T>
class span {
 public:
  using size_type = std::size_t;
  static constexpr size_type npos = -1;

  // Create an empty span.
  constexpr span() noexcept = default;

  // Create a span from a pointer and a size.
  constexpr span(T* first, size_type size) noexcept
      : data_(first), size_(size) {}

  // Create a span from a plain array.
  template <size_type n>
  constexpr span(T (&array)[n]) noexcept : data_(array), size_(n) {}

  // Create a span from any compatible container (including other span types).
  template <typename Container,
            typename = std::enable_if_t<detail::span_like_v<Container, T>>>
  constexpr span(Container& container) noexcept
      : data_(container.data()), size_(container.size()) {}

  constexpr T* data() const noexcept { return data_; }
  constexpr size_type size() const noexcept { return size_; }
  constexpr bool empty() const noexcept { return size_ == 0; }
  constexpr T& operator[](size_type index) const noexcept {
    assert(index < size_);
    return data_[index];
  }

  constexpr span subspan(size_type offset = 0,
                         size_type size = npos) const noexcept {
    assert(offset <= size_);
    return span(data_ + offset, std::min(size, size_ - offset));
  }

 private:
  T* data_;
  std::size_t size_;
};

}  // namespace util
