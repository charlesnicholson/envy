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
struct match : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
match(Ts...) -> match<Ts...>;

}  // namespace envy
