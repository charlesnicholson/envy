#pragma once

extern "C" {
#include "lua.h"
}

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace envy {

using lua_state_ptr = std::unique_ptr<lua_State, void (*)(lua_State *)>;

lua_state_ptr lua_make();
void lua_add_envy(lua_state_ptr const &state);  // install envy logging + globals
bool lua_run_file(lua_state_ptr const &state, std::filesystem::path const &path);
bool lua_run_string(lua_state_ptr const &state, char const *script);

struct lua_value;
using lua_table = std::unordered_map<std::string, lua_value>;

using lua_variant =
    std::variant<std::monostate, bool, int64_t, double, std::string, lua_table>;

struct lua_value {
  lua_variant v;

  lua_value();
  explicit lua_value(lua_variant var);

  bool is_nil() const;
  bool is_bool() const;
  bool is_integer() const;
  bool is_number() const;
  bool is_string() const;
  bool is_table() const;
};

lua_value lua_stack_to_value(lua_State *L, int index);
std::optional<lua_value> lua_global_to_value(lua_State *L, char const *name);

void value_to_lua_stack(lua_State *L, lua_value const &val);
void value_to_lua_global(lua_State *L, char const *name, lua_value const &val);

}  // namespace envy
