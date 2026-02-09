#pragma once

#include "sol/sol.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace envy {

using sol_state_ptr = std::unique_ptr<sol::state>;
sol_state_ptr sol_util_make_lua_state();  // with std libs

namespace detail {

template <typename T>
constexpr std::string_view type_name_for_error() {
  if constexpr (std::is_same_v<T, bool>) {
    return "boolean";
  } else if constexpr (std::is_same_v<T, std::string>) {
    return "string";
  } else if constexpr (std::is_same_v<T, sol::table>) {
    return "table";
  } else if constexpr (std::is_same_v<T, sol::protected_function> ||
                       std::is_same_v<T, sol::function>) {
    return "function";
  } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
    return "number";
  } else {
    return "value";
  }
}

}  // namespace detail

inline std::string sol_util_dump_table(sol::table const &tbl) {
  std::string result{ "{" };
  bool first{ true };
  for (auto const &pair : tbl) {
    if (!first) { result += ", "; }
    first = false;

    if (pair.first.is<std::string>()) {
      result += pair.first.as<std::string>();
    } else if (pair.first.is<int>()) {
      result += "[" + std::to_string(pair.first.as<int>()) + "]";
    } else {
      result += "?";
    }

    result += "=";
    if (pair.second.is<std::string>()) {
      std::string val{ pair.second.as<std::string>() };
      if (val.size() > 40) { val = val.substr(0, 37) + "..."; }
      result += "\"" + val + "\"";
    } else if (pair.second.is<sol::table>()) {
      result += "{...}";
    } else {
      result += sol::type_name(pair.first.lua_state(), pair.second.get_type());
    }
  }
  result += "}";
  return result;
}

template <typename T>
std::optional<T> sol_util_get_optional(sol::table const &table,
                                       std::string_view key,
                                       std::string_view context) {
  sol::optional<sol::object> obj = table[key];
  if (!obj || !obj->valid() || obj->get_type() == sol::type::lua_nil) {
    return std::nullopt;
  }

  if (!obj->is<T>()) {
    throw std::runtime_error(std::string(context) + ": " + std::string(key) +
                             " must be a " +
                             std::string(detail::type_name_for_error<T>()));
  }

  return obj->as<T>();
}

template <typename T>
T sol_util_get_required(sol::table const &table,
                        std::string_view key,
                        std::string_view context) {
  sol::optional<sol::object> obj = table[key];
  if (!obj || !obj->valid() || obj->get_type() == sol::type::lua_nil) {
    throw std::runtime_error(std::string(context) + ": " + std::string(key) +
                             " is required");
  }

  if (!obj->is<T>()) {
    throw std::runtime_error(std::string(context) + ": " + std::string(key) +
                             " must be a " +
                             std::string(detail::type_name_for_error<T>()));
  }

  return obj->as<T>();
}

template <typename T>
T sol_util_get_or_default(sol::table const &table,
                          std::string_view key,
                          T const &default_value,
                          std::string_view context) {
  auto opt{ sol_util_get_optional<T>(table, key, context) };
  return opt.value_or(default_value);
}

}  // namespace envy
