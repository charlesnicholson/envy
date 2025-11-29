#pragma once

#include "recipe_phase.h"
#include "util.h"

#include "sol/sol.hpp"

#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace envy {

class recipe_spec_pool;

struct recipe_spec : unmovable {
 private:
  struct ctor_tag {
    ctor_tag() = default;
    friend class recipe_spec_pool;
  };

 public:
  ~recipe_spec() = default;

  struct remote_source {
    std::string url;
    std::string sha256;
    std::optional<std::string> subdir;  // Path within archive to recipe entry point
  };

  struct local_source {
    std::filesystem::path file_path;  // Can be file or directory
  };

  struct git_source {
    std::string url;
    std::string ref;  // commit SHA or committish
    std::optional<std::string> subdir;
  };

  struct fetch_function {};  // Recipe defines custom fetch()

  struct weak_ref {};  // Reference-only or weak dependency (no source)

  using source_t =
      std::variant<remote_source, local_source, git_source, fetch_function, weak_ref>;

  recipe_spec(ctor_tag,
              std::string identity,
              source_t source,
              std::string serialized_options,
              std::optional<recipe_phase> needed_by,
              recipe_spec const *parent,
              recipe_spec *weak,
              std::vector<recipe_spec *> source_dependencies);

  std::string identity;  // "namespace.name@version"
  source_t source;
  std::string serialized_options;  // Serialized Lua table literal (empty "{}" if none)
  std::optional<recipe_phase> needed_by;         // Phase dependency annotation
  mutable recipe_spec const *parent{ nullptr };  // Owning parent spec
  recipe_spec *weak{ nullptr };                  // Weak fallback recipe (if any)

  // Custom source fetch (nested source dependencies)
  std::vector<recipe_spec *> source_dependencies{};  // Needed for fetching this recipe

  // Parse recipe_spec from Sol2 object (allocates via pool)
  static recipe_spec *parse(sol::object const &lua_val,
                            std::filesystem::path const &base_path,
                            bool allow_weak_without_source = false);

  // Parse recipe_spec directly from Lua stack (for tables containing functions)
  // Used primarily for testing; production code should use parse() with sol::object
  static recipe_spec *parse_from_stack(sol::state_view lua,
                                       int index,
                                       std::filesystem::path const &base_path,
                                       bool allow_weak_without_source = false);

  // Serialize sol::object to canonical string for stable recipe option hashing
  static std::string serialize_option_table(sol::object const &val);

  // Format canonical key: "identity" or "identity{opt=val,...}"
  // Used for logging, result maps, and any place needing a unique recipe identifier
  static std::string format_key(std::string const &identity,
                                std::string const &serialized_options);

  std::string format_key() const;
  bool is_remote() const;
  bool is_local() const;
  bool is_git() const;
  bool has_fetch_function() const;
  bool is_weak_reference() const;

  // Look up source.fetch function for a dependency from Lua state's dependencies global
  // Pushes the function onto the stack if found, returns true
  // Returns false if not found (leaves stack unchanged)
  static bool lookup_and_push_source_fetch(sol::state_view lua,
                                           std::string const &dep_identity);

  static void set_pool(recipe_spec_pool *pool);
  static recipe_spec_pool *pool();

 private:
  friend class recipe_spec_pool;
  static recipe_spec_pool *pool_;
};

class recipe_spec_pool {
 public:
  template <class... Args>
  recipe_spec *emplace(Args &&...args) {
    std::lock_guard const lock(mutex_);
    storage_.emplace_back(recipe_spec::ctor_tag{}, std::forward<Args>(args)...);
    return &storage_.back();
  }

 private:
  std::mutex mutex_;
  std::deque<recipe_spec> storage_;
};

bool operator==(recipe_spec::remote_source const &lhs,
                recipe_spec::remote_source const &rhs);
bool operator==(recipe_spec::local_source const &lhs,
                recipe_spec::local_source const &rhs);
bool operator==(recipe_spec::git_source const &lhs, recipe_spec::git_source const &rhs);

}  // namespace envy
