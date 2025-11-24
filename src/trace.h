#pragma once

#include "recipe_phase.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace envy {

namespace trace_events {

struct phase_blocked {
  std::string recipe;
  recipe_phase blocked_at_phase;
  std::string waiting_for;
  recipe_phase target_phase;
};

struct phase_unblocked {
  std::string recipe;
  recipe_phase unblocked_at_phase;
  std::string dependency;
};

struct dependency_added {
  std::string parent;
  std::string dependency;
  recipe_phase needed_by;
};

struct phase_start {
  std::string recipe;
  recipe_phase phase;
};

struct phase_complete {
  std::string recipe;
  recipe_phase phase;
  std::int64_t duration_ms;
};

struct thread_start {
  std::string recipe;
  recipe_phase target_phase;
};

struct thread_complete {
  std::string recipe;
  recipe_phase final_phase;
};

struct recipe_registered {
  std::string recipe;
  std::string key;
  bool has_dependencies;
};

struct target_extended {
  std::string recipe;
  recipe_phase old_target;
  recipe_phase new_target;
};

struct lua_ctx_run_start {
  std::string recipe;
  std::string command;
  std::string cwd;
};

struct lua_ctx_run_complete {
  std::string recipe;
  int exit_code;
  std::int64_t duration_ms;
};

struct lua_ctx_fetch_start {
  std::string recipe;
  std::string url;
  std::string destination;
};

struct lua_ctx_fetch_complete {
  std::string recipe;
  std::string url;
  std::int64_t bytes_downloaded;
  std::int64_t duration_ms;
};

struct lua_ctx_extract_start {
  std::string recipe;
  std::string archive_path;
  std::string destination;
};

struct lua_ctx_extract_complete {
  std::string recipe;
  std::int64_t files_extracted;
  std::int64_t duration_ms;
};

struct cache_hit {
  std::string recipe;
  std::string cache_key;
  std::string asset_path;
};

struct cache_miss {
  std::string recipe;
  std::string cache_key;
};

struct lock_acquired {
  std::string recipe;
  std::string lock_path;
  std::int64_t wait_duration_ms;
};

struct lock_released {
  std::string recipe;
  std::string lock_path;
  std::int64_t hold_duration_ms;
};

struct fetch_file_start {
  std::string recipe;
  std::string url;
  std::string destination;
};

struct fetch_file_complete {
  std::string recipe;
  std::string url;
  std::int64_t bytes_downloaded;
  std::int64_t duration_ms;
  bool from_cache;
};

}  // namespace trace_events

using trace_event_t = std::variant<trace_events::phase_blocked,
                                   trace_events::phase_unblocked,
                                   trace_events::dependency_added,
                                   trace_events::phase_start,
                                   trace_events::phase_complete,
                                   trace_events::thread_start,
                                   trace_events::thread_complete,
                                   trace_events::recipe_registered,
                                   trace_events::target_extended,
                                   trace_events::lua_ctx_run_start,
                                   trace_events::lua_ctx_run_complete,
                                   trace_events::lua_ctx_fetch_start,
                                   trace_events::lua_ctx_fetch_complete,
                                   trace_events::lua_ctx_extract_start,
                                   trace_events::lua_ctx_extract_complete,
                                   trace_events::cache_hit,
                                   trace_events::cache_miss,
                                   trace_events::lock_acquired,
                                   trace_events::lock_released,
                                   trace_events::fetch_file_start,
                                   trace_events::fetch_file_complete>;

std::string_view trace_event_name(trace_event_t const &event);
std::string trace_event_to_string(trace_event_t const &event);

namespace tui {
extern bool g_trace_enabled;
void trace(trace_event_t event);
}  // namespace tui

struct phase_trace_scope {
  std::string recipe;
  recipe_phase phase;
  std::chrono::steady_clock::time_point start;

  phase_trace_scope(std::string recipe_identity,
                    recipe_phase phase_value,
                    std::chrono::steady_clock::time_point start_time);
  ~phase_trace_scope();
};

}  // namespace envy

#define ENVY_TRACE_UNLIKELY [[unlikely]]

#define ENVY_TRACE_EMIT(event_expr) \
  do { \
    if (::envy::tui::g_trace_enabled) ENVY_TRACE_UNLIKELY { \
        ::envy::tui::trace event_expr; \
      } \
  } while (0)

#define ENVY_TRACE_PHASE_BLOCKED(recipe_value, \
                                 blocked_phase_value, \
                                 waiting_value, \
                                 target_phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_blocked{ \
      .recipe = (recipe_value), \
      .blocked_at_phase = (blocked_phase_value), \
      .waiting_for = (waiting_value), \
      .target_phase = (target_phase_value), \
  }))

#define ENVY_TRACE_PHASE_UNBLOCKED(recipe_value, unblocked_phase_value, dependency_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_unblocked{ \
      .recipe = (recipe_value), \
      .unblocked_at_phase = (unblocked_phase_value), \
      .dependency = (dependency_value), \
  }))

#define ENVY_TRACE_DEPENDENCY_ADDED(parent_value, dependency_value, needed_by_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::dependency_added{ \
      .parent = (parent_value), \
      .dependency = (dependency_value), \
      .needed_by = (needed_by_value), \
  }))

#define ENVY_TRACE_PHASE_START(recipe_value, phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_start{ \
      .recipe = (recipe_value), \
      .phase = (phase_value), \
  }))

#define ENVY_TRACE_PHASE_COMPLETE(recipe_value, phase_value, duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_complete{ \
      .recipe = (recipe_value), \
      .phase = (phase_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_THREAD_START(recipe_value, target_phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::thread_start{ \
      .recipe = (recipe_value), \
      .target_phase = (target_phase_value), \
  }))

#define ENVY_TRACE_THREAD_COMPLETE(recipe_value, final_phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::thread_complete{ \
      .recipe = (recipe_value), \
      .final_phase = (final_phase_value), \
  }))

#define ENVY_TRACE_RECIPE_REGISTERED(recipe_value, key_value, has_dependencies_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::recipe_registered{ \
      .recipe = (recipe_value), \
      .key = (key_value), \
      .has_dependencies = (has_dependencies_value), \
  }))

#define ENVY_TRACE_TARGET_EXTENDED(recipe_value, old_target_value, new_target_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::target_extended{ \
      .recipe = (recipe_value), \
      .old_target = (old_target_value), \
      .new_target = (new_target_value), \
  }))

#define ENVY_TRACE_LUA_CTX_RUN_START(recipe_value, command_value, cwd_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_run_start{ \
      .recipe = (recipe_value), \
      .command = (command_value), \
      .cwd = (cwd_value), \
  }))

#define ENVY_TRACE_LUA_CTX_RUN_COMPLETE(recipe_value, exit_code_value, duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_run_complete{ \
      .recipe = (recipe_value), \
      .exit_code = (exit_code_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_LUA_CTX_FETCH_START(recipe_value, url_value, destination_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_fetch_start{ \
      .recipe = (recipe_value), \
      .url = (url_value), \
      .destination = (destination_value), \
  }))

#define ENVY_TRACE_LUA_CTX_FETCH_COMPLETE(recipe_value, \
                                          url_value, \
                                          bytes_downloaded_value, \
                                          duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_fetch_complete{ \
      .recipe = (recipe_value), \
      .url = (url_value), \
      .bytes_downloaded = (bytes_downloaded_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_LUA_CTX_EXTRACT_START(recipe_value, \
                                         archive_path_value, \
                                         destination_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_extract_start{ \
      .recipe = (recipe_value), \
      .archive_path = (archive_path_value), \
      .destination = (destination_value), \
  }))

#define ENVY_TRACE_LUA_CTX_EXTRACT_COMPLETE(recipe_value, \
                                            files_extracted_value, \
                                            duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_extract_complete{ \
      .recipe = (recipe_value), \
      .files_extracted = (files_extracted_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_CACHE_HIT(recipe_value, cache_key_value, asset_path_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::cache_hit{ \
      .recipe = (recipe_value), \
      .cache_key = (cache_key_value), \
      .asset_path = (asset_path_value), \
  }))

#define ENVY_TRACE_CACHE_MISS(recipe_value, cache_key_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::cache_miss{ \
      .recipe = (recipe_value), \
      .cache_key = (cache_key_value), \
  }))

#define ENVY_TRACE_LOCK_ACQUIRED(recipe_value, lock_path_value, wait_duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lock_acquired{ \
      .recipe = (recipe_value), \
      .lock_path = (lock_path_value), \
      .wait_duration_ms = (wait_duration_value), \
  }))

#define ENVY_TRACE_LOCK_RELEASED(recipe_value, lock_path_value, hold_duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lock_released{ \
      .recipe = (recipe_value), \
      .lock_path = (lock_path_value), \
      .hold_duration_ms = (hold_duration_value), \
  }))

#define ENVY_TRACE_FETCH_FILE_START(recipe_value, url_value, destination_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::fetch_file_start{ \
      .recipe = (recipe_value), \
      .url = (url_value), \
      .destination = (destination_value), \
  }))

#define ENVY_TRACE_FETCH_FILE_COMPLETE(recipe_value, \
                                       url_value, \
                                       bytes_downloaded_value, \
                                       duration_value, \
                                       from_cache_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::fetch_file_complete{ \
      .recipe = (recipe_value), \
      .url = (url_value), \
      .bytes_downloaded = (bytes_downloaded_value), \
      .duration_ms = (duration_value), \
      .from_cache = (from_cache_value), \
  }))
