#include "lua_envy_options.h"

#include "lua_envy.h"

#include "semver.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace envy {
namespace {

struct range_constraint {
  enum class op { ge, gt, le, lt, eq };
  op comparison;
  double value;
};

std::vector<range_constraint> parse_numeric_range(std::string_view range_str) {
  std::vector<range_constraint> constraints;
  std::string_view sv{ range_str };

  while (!sv.empty()) {
    while (!sv.empty() && sv.front() == ' ') { sv.remove_prefix(1); }
    if (sv.empty()) { break; }

    range_constraint::op comparison;
    if (sv.starts_with(">=")) {
      comparison = range_constraint::op::ge;
      sv.remove_prefix(2);
    } else if (sv.starts_with(">")) {
      comparison = range_constraint::op::gt;
      sv.remove_prefix(1);
    } else if (sv.starts_with("<=")) {
      comparison = range_constraint::op::le;
      sv.remove_prefix(2);
    } else if (sv.starts_with("<")) {
      comparison = range_constraint::op::lt;
      sv.remove_prefix(1);
    } else if (sv.starts_with("==")) {
      comparison = range_constraint::op::eq;
      sv.remove_prefix(2);
    } else {
      throw std::runtime_error("invalid numeric range token near '" + std::string(sv) +
                               "'");
    }

    while (!sv.empty() && sv.front() == ' ') { sv.remove_prefix(1); }

    // Find end of number token
    auto end{ sv.begin() };
    bool has_dot{ false };
    if (end != sv.end() && (*end == '-' || *end == '+')) { ++end; }
    while (end != sv.end() &&
           ((*end >= '0' && *end <= '9') || (*end == '.' && !has_dot))) {
      if (*end == '.') { has_dot = true; }
      ++end;
    }

    std::string_view num_str{ sv.data(), static_cast<size_t>(end - sv.begin()) };
    if (num_str.empty()) {
      throw std::runtime_error("invalid numeric range token near '" + std::string(sv) +
                               "'");
    }

    std::string const num_s{ num_str };
    char *end_ptr{ nullptr };
    double const val{ std::strtod(num_s.c_str(), &end_ptr) };
    if (end_ptr != num_s.c_str() + num_s.size()) {
      throw std::runtime_error("invalid numeric range token '" + num_s + "'");
    }

    constraints.push_back({ comparison, val });
    sv.remove_prefix(static_cast<size_t>(end - sv.begin()));
  }

  return constraints;
}

bool numeric_constraint_satisfied(range_constraint const &c, double val) {
  switch (c.comparison) {
    case range_constraint::op::ge: return val >= c.value;
    case range_constraint::op::gt: return val > c.value;
    case range_constraint::op::le: return val <= c.value;
    case range_constraint::op::lt: return val < c.value;
    case range_constraint::op::eq: return val == c.value;
  }
  return false;
}

void validate_single_option(std::string const &key,
                            sol::table const &constraint,
                            sol::object const &value,
                            std::string const &identity) {
  auto const ctx{ "option '" + key + "'" };

  // semver check
  bool const is_semver{ constraint["semver"].valid() &&
                        constraint["semver"].get_type() == sol::type::boolean &&
                        constraint["semver"].get<bool>() };

  if (is_semver) {
    if (value.get_type() != sol::type::string) {
      throw std::runtime_error(ctx + " is not valid semver (must be a string) for " +
                               identity);
    }
    std::string const val_str{ value.as<std::string>() };
    semver::version<> ver;
    if (!semver::parse(val_str, ver)) {
      throw std::runtime_error(ctx + " '" + val_str + "' is not valid semver for " +
                               identity);
    }

    // semver range check
    sol::object range_obj{ constraint["range"] };
    if (range_obj.valid() && range_obj.get_type() == sol::type::string) {
      std::string const range_str{ range_obj.as<std::string>() };
      semver::range_set<> rs;
      if (!semver::parse(range_str, rs)) {
        throw std::runtime_error(ctx + " has invalid range '" + range_str + "' for " +
                                 identity);
      }
      if (!rs.contains(ver, semver::version_compare_option::include_prerelease)) {
        throw std::runtime_error(ctx + " '" + val_str + "' does not satisfy range '" +
                                 range_str + "' for " + identity);
      }
    }
  } else {
    // numeric range check (non-semver)
    sol::object range_obj{ constraint["range"] };
    if (range_obj.valid() && range_obj.get_type() == sol::type::string) {
      std::string const range_str{ range_obj.as<std::string>() };
      auto constraints{ parse_numeric_range(range_str) };

      double num_val{ 0.0 };
      if (value.get_type() == sol::type::number) {
        num_val = value.as<double>();
      } else if (value.get_type() == sol::type::string) {
        std::string const val_str{ value.as<std::string>() };
        char *end_ptr{ nullptr };
        num_val = std::strtod(val_str.c_str(), &end_ptr);
        if (end_ptr != val_str.c_str() + val_str.size()) {
          throw std::runtime_error(ctx + " value '" + val_str + "' is not numeric for " +
                                   identity);
        }
      } else {
        throw std::runtime_error(ctx + " value is not numeric for " + identity);
      }

      for (auto const &c : constraints) {
        if (!numeric_constraint_satisfied(c, num_val)) {
          std::ostringstream oss;
          oss << ctx << " value " << num_val << " does not satisfy range '" << range_str
              << "' for " << identity;
          throw std::runtime_error(oss.str());
        }
      }
    }
  }

  // custom validate function
  sol::object validate_fn_obj{ constraint["validate"] };
  if (validate_fn_obj.valid() && validate_fn_obj.get_type() == sol::type::function) {
    sol::protected_function validate_fn{ validate_fn_obj.as<sol::protected_function>() };
    sol::protected_function_result result{ validate_fn(value) };
    if (!result.valid()) {
      sol::error err = result;
      throw std::runtime_error(ctx + " validation failed: " + std::string(err.what()) +
                               " for " + identity);
    }
    sol::object ret{ result };
    sol::type const ret_type{ ret.get_type() };
    switch (ret_type) {
      case sol::type::lua_nil: break;
      case sol::type::boolean:
        if (!ret.as<bool>()) {
          throw std::runtime_error(ctx + " validation failed for " + identity);
        }
        break;
      case sol::type::string:
        throw std::runtime_error(ctx + " validation failed: " + ret.as<std::string>() +
                                 " for " + identity);
      default:
        throw std::runtime_error(ctx + " validate must return nil/true/false/string for " +
                                 identity);
    }
  }
}

}  // namespace

void validate_options_schema(sol::table const &schema,
                             sol::object const &opts,
                             std::string const &identity) {
  // opts should be a table (or nil for empty options)
  sol::table opts_table;
  bool const has_opts{ opts.valid() && opts.get_type() == sol::type::table };
  if (has_opts) { opts_table = opts.as<sol::table>(); }

  // Check each schema entry
  for (auto const &[key_obj, constraint_obj] : schema) {
    if (key_obj.get_type() != sol::type::string) { continue; }
    std::string const key{ key_obj.as<std::string>() };

    if (constraint_obj.get_type() != sol::type::table) {
      throw std::runtime_error("OPTIONS schema entry for '" + key +
                               "' must be a table for " + identity);
    }
    sol::table const constraint{ constraint_obj.as<sol::table>() };

    // Check required
    bool const required{ constraint["required"].valid() &&
                         constraint["required"].get_type() == sol::type::boolean &&
                         constraint["required"].get<bool>() };

    sol::object value{ has_opts ? opts_table[key] : sol::object(sol::lua_nil) };
    bool const present{ value.valid() && value.get_type() != sol::type::lua_nil };

    if (required && !present) {
      throw std::runtime_error("option '" + key + "' is required for " + identity);
    }

    if (present) { validate_single_option(key, constraint, value, identity); }
  }

  // Reject unknown options
  if (has_opts) {
    for (auto const &[key_obj, value_obj] : opts_table) {
      if (key_obj.get_type() != sol::type::string) { continue; }
      std::string const key{ key_obj.as<std::string>() };

      sol::object schema_entry{ schema[key] };
      if (!schema_entry.valid() || schema_entry.get_type() == sol::type::lua_nil) {
        std::ostringstream oss;
        oss << "unknown option '" << key << "' for " << identity << "; valid options:";
        for (auto const &[sk, sv] : schema) {
          if (sk.get_type() == sol::type::string) { oss << " " << sk.as<std::string>(); }
        }
        throw std::runtime_error(oss.str());
      }
    }
  }
}

void lua_envy_options_install(sol::table &envy_table) {
  envy_table["options"] = [](sol::this_state ts, sol::table schema) {
    sol::state_view lua{ ts };
    sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

    // Derive identity from globals if available
    std::string identity{ "unknown" };
    sol::object id_obj{ lua.globals()["IDENTITY"] };
    if (id_obj.valid() && id_obj.get_type() == sol::type::string) {
      identity = id_obj.as<std::string>();
    }

    validate_options_schema(schema, opts, identity);
  };
}

}  // namespace envy
