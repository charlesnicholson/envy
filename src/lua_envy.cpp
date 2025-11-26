#include "lua_envy.h"

#include "shell.h"
#include "tui.h"

extern "C" {
#include "lua.h"
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
    local open_count = 0
    local i = 1
    while i <= #str do
      if str:sub(i, i+1) == "{{" then
        open_count = open_count + 1
        i = i + 2
      elseif str:sub(i, i+1) == "}}" then
        open_count = open_count - 1
        if open_count < 0 then
          error("envy.template: unmatched '}}' at position " .. i, 2)
        end
        i = i + 2
      else
        i = i + 1
      end
    end
    if open_count > 0 then
      error("envy.template: unmatched '{{' (missing closing '}}')", 2)
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

}  // namespace

void lua_envy_install(sol::state &lua) {
  // Platform detection
  char const *platform{ nullptr };
  char const *arch{ nullptr };

#if defined(__APPLE__) && defined(__MACH__)
  platform = "darwin";
#if defined(__arm64__)
  arch = "arm64";
#elif defined(__x86_64__)
  arch = "x86_64";
#endif
#elif defined(__linux__)
  platform = "linux";
#if defined(__aarch64__)
  arch = "aarch64";
#elif defined(__x86_64__)
  arch = "x86_64";
#endif
#elif defined(_WIN32)
  platform = "windows";
#if defined(_M_ARM64)
  arch = "arm64";
#elif defined(_M_X64)
  arch = "x86_64";
#endif
#endif

  std::string const platform_arch{ std::string{ platform } + "-" + arch };

  // Platform globals
  lua["ENVY_PLATFORM"] = platform;
  lua["ENVY_ARCH"] = arch;
  lua["ENVY_PLATFORM_ARCH"] = platform_arch;

  // Override print to route through TUI
  lua["print"] = [](sol::variadic_args va) {
    std::ostringstream oss;
    bool first{ true };
    for (auto arg : va) {
      if (!first) oss << '\t';
      oss << luaL_tolstring(arg.lua_state(), arg.stack_index(), nullptr);
      lua_pop(arg.lua_state(), 1);  // Pop result from luaL_tolstring
      first = false;
    }
    tui::info("%s", oss.str().c_str());
  };

  auto envy_table{ lua.create_table() };  // envy table with logging functions
  envy_table["trace"] = [](std::string_view msg) { tui::debug("%s", msg.data()); };
  envy_table["debug"] = [](std::string_view msg) { tui::debug("%s", msg.data()); };
  envy_table["info"] = [](std::string_view msg) { tui::info("%s", msg.data()); };
  envy_table["warn"] = [](std::string_view msg) { tui::warn("%s", msg.data()); };
  envy_table["error"] = [](std::string_view msg) { tui::error("%s", msg.data()); };
  envy_table["stdout"] = [](std::string_view msg) { tui::print_stdout("%s", msg.data()); };

  // envy.template (load Lua code)
  sol::protected_function_result result{ lua.safe_script(kEnvyTemplateLua,
                                                         sol::script_pass_on_error) };
  if (result.valid()) {
    envy_table["template"] = result;
  } else {
    sol::error err = result;
    tui::error("Failed to load envy.template: %s", err.what());
  }

  lua["envy"] = envy_table;

  auto shell_table{ lua.create_table() };
  lua_State *L{ lua.lua_state() };

#if defined(__APPLE__) || defined(__linux__)
  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(shell_choice::bash)));
  shell_table["BASH"] = sol::stack_object{ L, -1 };
  lua_pop(L, 1);

  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(shell_choice::sh)));
  shell_table["SH"] = sol::stack_object{ L, -1 };
  lua_pop(L, 1);
#endif

#if defined(_WIN32)
  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(shell_choice::cmd)));
  shell_table["CMD"] = sol::stack_object{ L, -1 };
  lua_pop(L, 1);

  lua_pushlightuserdata(
      L,
      reinterpret_cast<void *>(static_cast<uintptr_t>(shell_choice::powershell)));
  shell_table["POWERSHELL"] = sol::stack_object{ L, -1 };
  lua_pop(L, 1);
#endif

  lua["ENVY_SHELL"] = shell_table;
}

}  // namespace envy
