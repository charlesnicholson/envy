#include "spec_util.h"

#include "lua_envy.h"
#include "sol_util.h"

#include <stdexcept>

namespace envy {

std::string extract_spec_identity(std::filesystem::path const &spec_path,
                                  std::filesystem::path const &package_path_root) {
  if (!std::filesystem::exists(spec_path)) {
    throw std::runtime_error("spec file not found: " + spec_path.string());
  }

  auto lua{ sol_util_make_lua_state() };
  lua_envy_install(*lua);

  // Configure package.path for bundle-local requires if root provided
  if (!package_path_root.empty()) {
    std::string const root{ package_path_root.string() };
    sol::table package_table{ (*lua)["package"] };
    std::string const current_path{ package_table["path"].get_or<std::string>("") };
    package_table["path"] = root + "/?.lua;" + root + "/?/init.lua;" + current_path;
  }

  // Execute spec file
  sol::protected_function_result result{
    lua->safe_script_file(spec_path.string(), sol::script_pass_on_error)
  };

  if (!result.valid()) {
    sol::error err{ result };
    throw std::runtime_error("failed to execute spec '" + spec_path.string() +
                             "': " + err.what());
  }

  // Extract IDENTITY
  sol::object id_obj{ (*lua)["IDENTITY"] };

  if (!id_obj.valid() || id_obj.get_type() == sol::type::lua_nil) {
    throw std::runtime_error("spec '" + spec_path.string() +
                             "' is missing required IDENTITY field");
  }

  if (!id_obj.is<std::string>()) {
    throw std::runtime_error("spec '" + spec_path.string() +
                             "': IDENTITY must be a string");
  }

  std::string identity{ id_obj.as<std::string>() };

  if (identity.empty()) {
    throw std::runtime_error("spec '" + spec_path.string() +
                             "': IDENTITY cannot be empty");
  }

  return identity;
}

}  // namespace envy
