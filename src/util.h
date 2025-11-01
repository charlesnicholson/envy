#pragma once

#include <concepts>

namespace envy {

template <typename T, typename... Types>
concept one_of = (std::same_as<T, Types> || ...);

struct uncopyable {
  uncopyable() = default;
  uncopyable(uncopyable &&) = default;
  uncopyable &operator=(uncopyable &&) = default;
};

struct unmovable {
  unmovable() = default;
  unmovable(unmovable const &) = delete;
  unmovable &operator=(unmovable const &) = delete;
};

template <typename... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

}  // namespace envy
