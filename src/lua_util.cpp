#include "lua_util.h"

#include "tui.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <sstream>

namespace envy {
namespace {

constexpr char kEnvyTemplateLua[] = R"lua(
return function(str, values)
  if type(str) ~= "string" then
    error("envy.template: first argument must be a string", 2)
  end
  if type(values) ~= "table" then
    error("envy.template: second argument must be a table", 2)
  end

  local function normalize_key(raw)
    local trimmed = raw:match("^%s*(.-)%s*$")
    if not trimmed or trimmed == "" then
      error("envy.template: placeholder cannot be empty", 2)
    end
    if not trimmed:match("^[%a_][%w_]*$") then
      error("envy.template: placeholder '" .. trimmed .. "' contains invalid characters", 2)
    end
    return trimmed
  end

  local function ensure_pairs(str)
    local search_from = 1
    while true do
      local open_start = str:find("{{", search_from, true)
      if not open_start then break end
      local close_start = str:find("}}", open_start + 2, true)
      if not close_start then
        error("envy.template: unmatched '{{' at position " .. open_start, 2)
      end
      search_from = close_start + 2
    end
  end

  ensure_pairs(str)

  local function replacer(token)
    local key = normalize_key(token)
    local value = values[key]
    if value == nil then
      error("envy.template: missing value for placeholder '" .. key .. "'", 2)
    end
    return tostring(value)
  end

  return (str:gsub("{{(.-)}}", replacer))
end
)lua";

int lua_print_override(lua_State *lua) {
  int argc{ lua_gettop(lua) };
  std::ostringstream oss;

  for (int i{ 1 }; i <= argc; ++i) {
    if (i > 1) { oss << '\t'; }
    if (auto str{ luaL_tolstring(lua, i, nullptr) }) { oss << str; }
    lua_pop(lua, 1);
  }

  tui::info("%s", oss.str().c_str());
  return 0;
}

template <void tui_func(char const *, ...)>
int lua_print_tui(lua_State *lua) {
  tui_func("%s", luaL_checkstring(lua, 1));
  return 0;
}

bool push_envy_template(lua_State *lua) {
  if (luaL_loadstring(lua, kEnvyTemplateLua) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    tui::error("Failed to load envy.template helper: %s", err ? err : "unknown error");
    lua_pop(lua, 1);
    return false;
  }

  if (lua_pcall(lua, 0, 1, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    tui::error("Failed to initialize envy.template helper: %s",
               err ? err : "unknown error");
    lua_pop(lua, 1);
    return false;
  }

  return true;
}

}  // namespace

void lua_deleter::operator()(lua_State *lua) const {
  if (lua) lua_close(lua);
}

lua_state_ptr lua_make() {
  lua_State *lua{ luaL_newstate() };
  if (!lua) {
    tui::error("Failed to create Lua state");
    return lua_state_ptr{ nullptr };
  }

  luaL_openlibs(lua);
  return lua_state_ptr{ lua };
}

void lua_add_envy(lua_state_ptr const &state) {
  lua_State *lua{ state.get() };
  if (!lua) {
    tui::error("lua_add_envy called with null state");
    return;
  }

  // Platform detection
  char const *platform{ nullptr };
  char const *arch{ nullptr };

#if defined(__APPLE__) && defined(__MACH__)
  platform = "darwin";
#if defined(__arm64__)
  arch = "arm64";
#elif defined(__x86_64__)
  arch = "x86_64";
#else
  arch = "unknown";
#endif
#elif defined(__linux__)
  platform = "linux";
#if defined(__aarch64__)
  arch = "aarch64";
#elif defined(__x86_64__)
  arch = "x86_64";
#elif defined(__i386__)
  arch = "i386";
#else
  arch = "unknown";
#endif
#elif defined(_WIN32)
  platform = "windows";
#if defined(_M_ARM64)
  arch = "arm64";
#elif defined(_M_X64) || defined(_M_AMD64)
  arch = "x86_64";
#elif defined(_M_IX86)
  arch = "x86";
#else
  arch = "unknown";
#endif
#else
  platform = "unknown";
  arch = "unknown";
#endif
  std::string const platform_arch{ std::string{ platform } + "-" + arch };

  lua_pushstring(lua, platform);
  lua_setglobal(lua, "ENVY_PLATFORM");
  lua_pushstring(lua, arch);
  lua_setglobal(lua, "ENVY_ARCH");
  lua_pushlstring(lua, platform_arch.c_str(), platform_arch.size());
  lua_setglobal(lua, "ENVY_PLATFORM_ARCH");

  lua_pushcfunction(lua, lua_print_override);
  lua_setglobal(lua, "print");

  lua_newtable(lua);
  lua_pushcfunction(lua, lua_print_tui<tui::trace>);
  lua_setfield(lua, -2, "trace");
  lua_pushcfunction(lua, lua_print_tui<tui::debug>);
  lua_setfield(lua, -2, "debug");
  lua_pushcfunction(lua, lua_print_tui<tui::info>);
  lua_setfield(lua, -2, "info");
  lua_pushcfunction(lua, lua_print_tui<tui::warn>);
  lua_setfield(lua, -2, "warn");
  lua_pushcfunction(lua, lua_print_tui<tui::error>);
  lua_setfield(lua, -2, "error");
  lua_pushcfunction(lua, lua_print_tui<tui::print_stdout>);
  lua_setfield(lua, -2, "stdout");
  if (push_envy_template(lua)) { lua_setfield(lua, -2, "template"); }
  lua_setglobal(lua, "envy");
}

bool lua_run_file(lua_state_ptr const &state, std::filesystem::path const &path) {
  lua_State *lua{ state.get() };
  if (!lua) {
    tui::error("lua_run called with null state");
    return false;
  }

  if (int const load_status{ luaL_loadfile(lua, path.string().c_str()) };
      load_status != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    if (load_status == LUA_ERRFILE) {
      tui::error("Failed to open %s: %s",
                 path.string().c_str(),
                 err ? err : "unknown error");
    } else {
      tui::error("%s", err ? err : "unknown error");
    }
    lua_pop(lua, 1);
    return false;
  }

  if (lua_pcall(lua, 0, LUA_MULTRET, 0) != LUA_OK) {
    auto err{ lua_tostring(lua, -1) };
    tui::error("%s", err ? err : "unknown error");
    lua_pop(lua, 1);
    return false;
  }

  return true;
}

bool lua_run_string(lua_state_ptr const &state, char const *script) {
  lua_State *lua{ state.get() };
  if (!lua) {
    tui::error("lua_run_string called with null state");
    return false;
  }

  if (luaL_loadstring(lua, script) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    tui::error("Failed to load Lua script: %s", err ? err : "unknown error");
    lua_pop(lua, 1);
    return false;
  }

  if (lua_pcall(lua, 0, LUA_MULTRET, 0) != LUA_OK) {
    char const *err{ lua_tostring(lua, -1) };
    tui::error("Lua script execution failed: %s", err ? err : "unknown error");
    lua_pop(lua, 1);
    return false;
  }

  return true;
}

lua_value::lua_value() : v{ std::monostate{} } {}
lua_value::lua_value(lua_variant var) : v{ std::move(var) } {}

bool lua_value::is_nil() const { return std::holds_alternative<std::monostate>(v); }
bool lua_value::is_bool() const { return std::holds_alternative<bool>(v); }
bool lua_value::is_integer() const { return std::holds_alternative<int64_t>(v); }
bool lua_value::is_number() const { return std::holds_alternative<double>(v); }
bool lua_value::is_string() const { return std::holds_alternative<std::string>(v); }
bool lua_value::is_table() const { return std::holds_alternative<lua_table>(v); }

lua_value lua_stack_to_value(lua_State *L, int index) {
  int const type{ lua_type(L, index) };

  switch (type) {
    case LUA_TNIL: return lua_value{};

    case LUA_TBOOLEAN:
      return lua_value{ lua_variant{ static_cast<bool>(lua_toboolean(L, index)) } };

    case LUA_TNUMBER:
      if (lua_isinteger(L, index)) {
        return lua_value{ lua_variant{ lua_tointeger(L, index) } };
      } else {
        return lua_value{ lua_variant{ lua_tonumber(L, index) } };
      }

    case LUA_TSTRING: {
      size_t len{ 0 };
      char const *str{ lua_tolstring(L, index, &len) };
      return lua_value{ lua_variant{ std::string{ str, len } } };
    }

    case LUA_TTABLE: {
      lua_table table;

      // Normalize negative indices to positive (lua_next requires positive)
      int const abs_index{ index < 0 ? lua_gettop(L) + index + 1 : index };

      lua_pushnil(L);  // First key
      while (lua_next(L, abs_index) != 0) {
        // Key at -2, value at -1

        // Only process string keys
        if (lua_type(L, -2) == LUA_TSTRING) {
          size_t key_len{ 0 };
          char const *key_str{ lua_tolstring(L, -2, &key_len) };
          std::string key{ key_str, key_len };

          // Recursively convert value
          lua_value value{ lua_stack_to_value(L, -1) };
          table[std::move(key)] = std::move(value);
        }

        lua_pop(L, 1);  // Remove value, keep key for next iteration
      }

      return lua_value{ lua_variant{ std::move(table) } };
    }

    default:
      throw std::runtime_error(std::string("Unsupported Lua type: ") +
                               lua_typename(L, type));
  }
}

std::optional<lua_value> lua_global_to_value(lua_State *L, char const *name) {
  lua_getglobal(L, name);
  std::optional<lua_value> const result{
    lua_isnil(L, -1) ? std::nullopt : std::optional{ lua_stack_to_value(L, -1) }
  };

  lua_pop(L, 1);
  return result;
}

std::optional<std::vector<lua_value>> lua_global_to_array(lua_State *L, char const *name) {
  lua_getglobal(L, name);

  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return std::nullopt;
  }

  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    throw std::runtime_error(std::string{ "Global '" } + name + "' is not a table");
  }

  // First pass: check for any non-numeric keys
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    if (lua_type(L, -2) != LUA_TNUMBER) {
      lua_pop(L, 3);  //  value, key, table
      throw std::runtime_error(std::string{ "Table '" } + name +
                               "' contains non-numeric keys");
    }
    lua_pop(L, 1);  // pop value, keep key for next iteration
  }

  // Count total elements in table
  lua_pushnil(L);
  lua_Integer const total_count{ [&] {
    lua_Integer ttl;
    for (ttl = 0; lua_next(L, -2); ++ttl) { lua_pop(L, 1); }
    return ttl;
  }() };

  // Extract consecutive indices starting at 1
  std::vector<lua_value> result;
  result.reserve(total_count);

  for (lua_Integer i{ 1 }; i <= total_count; ++i) {
    lua_pushinteger(L, i);
    lua_gettable(L, -2);

    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);  // pop nil and table
      throw std::runtime_error(std::string{ "Table '" } + name +
                               "' is sparse (has gaps in numeric indices)");
    }

    result.push_back(lua_stack_to_value(L, -1));
    lua_pop(L, 1);
  }

  lua_pop(L, 1);  // pop table
  return result;
}

std::string lua_global_to_string(lua_State *L, char const *name) {
  auto val = lua_global_to_value(L, name);

  if (!val || val->is_nil()) {
    throw std::runtime_error(std::string("Recipe must declare '") + name + "' field");
  }

  auto const *str = val->get<std::string>();
  if (!str) {
    throw std::runtime_error(std::string("Recipe '") + name + "' field must be a string");
  }

  return *str;
}

void value_to_lua_stack(lua_State *L, lua_value const &val) {
  static_assert(
      std::is_same_v<std::variant_alternative_t<0, lua_variant>, std::monostate>);
  static_assert(std::is_same_v<std::variant_alternative_t<1, lua_variant>, bool>);
  static_assert(std::is_same_v<std::variant_alternative_t<2, lua_variant>, int64_t>);
  static_assert(std::is_same_v<std::variant_alternative_t<3, lua_variant>, double>);
  static_assert(std::is_same_v<std::variant_alternative_t<4, lua_variant>, std::string>);
  static_assert(std::is_same_v<std::variant_alternative_t<5, lua_variant>, lua_table>);

  switch (val.v.index()) {
    case 0: lua_pushnil(L); break;
    case 1: lua_pushboolean(L, std::get<bool>(val.v)); break;
    case 2: lua_pushinteger(L, std::get<int64_t>(val.v)); break;
    case 3: lua_pushnumber(L, std::get<double>(val.v)); break;

    case 4: {
      auto const &str{ std::get<std::string>(val.v) };
      lua_pushlstring(L, str.data(), str.size());
      break;
    }

    case 5: {
      auto const &table{ std::get<lua_table>(val.v) };
      lua_createtable(L, 0, static_cast<int>(table.size()));

      for (auto const &[key, value] : table) {
        lua_pushlstring(L, key.data(), key.size());
        value_to_lua_stack(L, value);  // Recursive
        lua_settable(L, -3);
      }
      break;
    }
  }
}

void value_to_lua_global(lua_State *L, char const *name, lua_value const &val) {
  value_to_lua_stack(L, val);
  lua_setglobal(L, name);
}

}  // namespace envy
