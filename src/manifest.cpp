#include "manifest.h"

#include "engine.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_shell.h"
#include "shell.h"
#include "tui.h"

#include <cstring>
#include <stdexcept>

namespace envy {

std::optional<std::filesystem::path> manifest::discover() {
  namespace fs = std::filesystem;

  auto cur{ fs::current_path() };

  for (;;) {
    auto const manifest_path{ cur / "envy.lua" };
    if (fs::exists(manifest_path)) { return manifest_path; }

    auto const git_path{ cur / ".git" };
    if (fs::exists(git_path) && fs::is_directory(git_path)) { return std::nullopt; }

    auto const parent{ cur.parent_path() };
    if (parent == cur) { return std::nullopt; }

    cur = parent;
  }
}

std::filesystem::path manifest::find_manifest_path(
    std::optional<std::filesystem::path> const &explicit_path) {
  if (explicit_path) {
    auto const path{ std::filesystem::absolute(*explicit_path) };
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error("manifest not found: " + path.string());
    }
    return path;
  } else {
    if (auto const discovered{ discover() }) { return *discovered; }
    throw std::runtime_error("manifest not found (discovery failed)");
  }
}

std::unique_ptr<manifest> manifest::load(std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest from file: %s", manifest_path.string().c_str());
  return load(util_load_file(manifest_path), manifest_path);
}

std::unique_ptr<manifest> manifest::load(std::vector<unsigned char> const &content,
                                         std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest (%zu bytes)", content.size());
  // Ensure null-termination for Lua (create string with guaranteed null terminator)
  std::string const script{ reinterpret_cast<char const *>(content.data()),
                            content.size() };

  auto state{ std::make_unique<sol::state>() };
  state->open_libraries(sol::lib::base,
                        sol::lib::package,
                        sol::lib::coroutine,
                        sol::lib::string,
                        sol::lib::os,
                        sol::lib::math,
                        sol::lib::table,
                        sol::lib::debug,
                        sol::lib::bit32,
                        sol::lib::io);
  lua_envy_install(*state);

  if (sol::protected_function_result const result{
          state->safe_script(script, sol::script_pass_on_error) };
      !result.valid()) {
    sol::error err = result;
    throw std::runtime_error(std::string("Failed to execute manifest script: ") +
                             err.what());
  }

  auto m{ std::make_unique<manifest>() };
  m->manifest_path = manifest_path;
  m->lua_ = std::move(state);  // Keep lua state alive for default_shell access

  sol::object packages_obj = (*m->lua_)["packages"];
  if (!packages_obj.valid() || packages_obj.get_type() != sol::type::table) {
    throw std::runtime_error("Manifest must define 'packages' global as a table");
  }

  sol::table packages_table = packages_obj.as<sol::table>();
  lua_State *L{ m->lua_->lua_state() };

  for (size_t i{ 1 }; i <= packages_table.size(); ++i) {
    sol::object pkg = packages_table[i];
    pkg.push(L);
    m->packages.push_back(recipe_spec::parse_from_stack(L, -1, manifest_path));
    lua_pop(L, 1);
  }

  return m;
}

std::unique_ptr<manifest> manifest::load(char const *script,
                                         std::filesystem::path const &manifest_path) {
  tui::debug("Loading manifest from C string");
  return load(std::vector<unsigned char>(script, script + std::strlen(script)),
              manifest_path);
}

default_shell_cfg_t manifest::get_default_shell(lua_ctx_common const *ctx) const {
  if (!lua_) { return std::nullopt; }

  sol::object default_shell_obj{ (*lua_)["default_shell"] };
  if (!default_shell_obj.valid()) { return std::nullopt; }

  // Helper to convert flat variant to nested variant structure
  auto const convert_parsed{
    [](std::variant<shell_choice, custom_shell_file, custom_shell_inline> const &parsed)
        -> default_shell_value {
      if (std::holds_alternative<shell_choice>(parsed)) {
        return std::get<shell_choice>(parsed);
      } else if (std::holds_alternative<custom_shell_file>(parsed)) {
        return custom_shell{ std::get<custom_shell_file>(parsed) };
      } else {
        return custom_shell{ std::get<custom_shell_inline>(parsed) };
      }
    }
  };

  if (default_shell_obj.is<sol::protected_function>()) {
    sol::protected_function default_shell_func{
      default_shell_obj.as<sol::protected_function>()
    };

    sol::table ctx_table{ lua_->create_table() };
    ctx_table["asset"] = make_ctx_asset(const_cast<lua_ctx_common *>(ctx));

    sol::protected_function_result result{ default_shell_func(ctx_table) };
    if (!result.valid()) {
      sol::error err{ result };
      throw std::runtime_error("default_shell function failed: " +
                               std::string{ err.what() });
    }

    return convert_parsed(
        parse_shell_config_from_lua(result.get<sol::object>(), "default_shell function"));
  }

  return convert_parsed(parse_shell_config_from_lua(default_shell_obj, "default_shell"));
}

}  // namespace envy
