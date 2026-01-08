#pragma once

#include "pkg_phase.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace envy {

namespace trace_events {

struct phase_blocked {
  std::string spec;
  pkg_phase blocked_at_phase;
  std::string waiting_for;
  pkg_phase target_phase;
};

struct phase_unblocked {
  std::string spec;
  pkg_phase unblocked_at_phase;
  std::string dependency;
};

struct dependency_added {
  std::string parent;
  std::string dependency;
  pkg_phase needed_by;
};

struct phase_start {
  std::string spec;
  pkg_phase phase;
};

struct phase_complete {
  std::string spec;
  pkg_phase phase;
  std::int64_t duration_ms;
};

struct thread_start {
  std::string spec;
  pkg_phase target_phase;
};

struct thread_complete {
  std::string spec;
  pkg_phase final_phase;
};

struct spec_registered {
  std::string spec;
  std::string key;
  bool has_dependencies;
};

struct target_extended {
  std::string spec;
  pkg_phase old_target;
  pkg_phase new_target;
};

struct lua_ctx_run_start {
  std::string spec;
  std::string command;
  std::string cwd;
};

struct lua_ctx_run_complete {
  std::string spec;
  int exit_code;
  std::int64_t duration_ms;
};

struct lua_ctx_fetch_start {
  std::string spec;
  std::string url;
  std::string destination;
};

struct lua_ctx_fetch_complete {
  std::string spec;
  std::string url;
  std::int64_t bytes_downloaded;
  std::int64_t duration_ms;
};

struct lua_ctx_extract_start {
  std::string spec;
  std::string archive_path;
  std::string destination;
};

struct lua_ctx_extract_complete {
  std::string spec;
  std::int64_t files_extracted;
  std::int64_t duration_ms;
};

struct lua_ctx_package_access {
  std::string spec;
  std::string target;
  pkg_phase current_phase;
  pkg_phase needed_by;
  bool allowed;
  std::string reason;
};

struct lua_ctx_product_access {
  std::string spec;
  std::string product;
  std::string provider;
  pkg_phase current_phase;
  pkg_phase needed_by;
  bool allowed;
  std::string reason;
};

struct lua_ctx_loadenv_spec_access {
  std::string spec;
  std::string target;
  std::string subpath;
  pkg_phase current_phase;
  pkg_phase needed_by;
  bool allowed;
  std::string reason;
};

struct cache_hit {
  std::string spec;
  std::string cache_key;
  std::string pkg_path;
  bool fast_path;
};

struct cache_miss {
  std::string spec;
  std::string cache_key;
};

struct lock_acquired {
  std::string spec;
  std::string lock_path;
  std::int64_t wait_duration_ms;
};

struct lock_released {
  std::string spec;
  std::string lock_path;
  std::int64_t hold_duration_ms;
};

struct fetch_file_start {
  std::string spec;
  std::string url;
  std::string destination;
};

struct fetch_file_complete {
  std::string spec;
  std::string url;
  std::int64_t bytes_downloaded;
  std::int64_t duration_ms;
  bool from_cache;
};

struct spec_fetch_counter_inc {
  std::string spec;
  int new_value;
};

struct spec_fetch_counter_dec {
  std::string spec;
  int new_value;
  bool was_completed;
};

struct execute_downloads_start {
  std::string spec;
  std::size_t thread_id;
  std::size_t num_files;
};

struct execute_downloads_complete {
  std::string spec;
  std::size_t thread_id;
  std::size_t num_files;
  std::int64_t duration_ms;
};

struct debug_marker {
  std::string spec;
  int marker_id;
};

struct cache_check_entry {
  std::string spec;
  std::string entry_dir;
  std::string check_location;  // "before_lock" or "after_lock"
};

struct cache_check_result {
  std::string spec;
  std::string entry_dir;
  bool is_complete;
  std::string check_location;  // "before_lock" or "after_lock"
};

struct directory_flushed {
  std::string spec;
  std::string dir_path;
};

struct file_touched {
  std::string spec;
  std::string file_path;
};

struct file_exists_check {
  std::string spec;
  std::string file_path;
  bool exists;
};

struct directory_flush_failed {
  std::string spec;
  std::string dir_path;
  std::string reason;
};

struct extract_archive_start {
  std::string spec;
  std::string archive_path;
  std::string destination;
  int strip_components;
};

struct extract_archive_complete {
  std::string spec;
  std::string archive_path;
  std::int64_t files_extracted;
  std::int64_t duration_ms;
};

struct product_transitive_check {
  std::string spec;
  std::string product;
  bool has_product_directly;
  std::size_t dependency_count;
};

struct product_transitive_check_dep {
  std::string spec;
  std::string product;
  std::string checking_dependency;
};

struct product_parsed {
  std::string spec;
  std::string product_name;
  std::string product_value;
};

}  // namespace trace_events

using trace_event_t = std::variant<trace_events::phase_blocked,
                                   trace_events::phase_unblocked,
                                   trace_events::dependency_added,
                                   trace_events::phase_start,
                                   trace_events::phase_complete,
                                   trace_events::thread_start,
                                   trace_events::thread_complete,
                                   trace_events::spec_registered,
                                   trace_events::target_extended,
                                   trace_events::lua_ctx_run_start,
                                   trace_events::lua_ctx_run_complete,
                                   trace_events::lua_ctx_fetch_start,
                                   trace_events::lua_ctx_fetch_complete,
                                   trace_events::lua_ctx_extract_start,
                                   trace_events::lua_ctx_extract_complete,
                                   trace_events::lua_ctx_package_access,
                                   trace_events::lua_ctx_product_access,
                                   trace_events::lua_ctx_loadenv_spec_access,
                                   trace_events::cache_hit,
                                   trace_events::cache_miss,
                                   trace_events::lock_acquired,
                                   trace_events::lock_released,
                                   trace_events::fetch_file_start,
                                   trace_events::fetch_file_complete,
                                   trace_events::spec_fetch_counter_inc,
                                   trace_events::spec_fetch_counter_dec,
                                   trace_events::execute_downloads_start,
                                   trace_events::execute_downloads_complete,
                                   trace_events::debug_marker,
                                   trace_events::cache_check_entry,
                                   trace_events::cache_check_result,
                                   trace_events::directory_flushed,
                                   trace_events::file_touched,
                                   trace_events::file_exists_check,
                                   trace_events::directory_flush_failed,
                                   trace_events::extract_archive_start,
                                   trace_events::extract_archive_complete,
                                   trace_events::product_transitive_check,
                                   trace_events::product_transitive_check_dep,
                                   trace_events::product_parsed>;

std::string_view trace_event_name(trace_event_t const &event);
std::string trace_event_to_string(trace_event_t const &event);
std::string trace_event_to_json(trace_event_t const &event);

namespace tui {
extern bool g_trace_enabled;
void trace(trace_event_t event);

inline bool trace_enabled() { return g_trace_enabled; }
}  // namespace tui

struct phase_trace_scope {
  std::string spec;
  pkg_phase phase;
  std::chrono::steady_clock::time_point start;

  phase_trace_scope(std::string pkg_identity,
                    pkg_phase phase_value,
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

#define ENVY_TRACE_PHASE_BLOCKED(spec_value, \
                                 blocked_phase_value, \
                                 waiting_value, \
                                 target_phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_blocked{ \
      .spec = (spec_value), \
      .blocked_at_phase = (blocked_phase_value), \
      .waiting_for = (waiting_value), \
      .target_phase = (target_phase_value), \
  }))

#define ENVY_TRACE_PHASE_UNBLOCKED(spec_value, unblocked_phase_value, dependency_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_unblocked{ \
      .spec = (spec_value), \
      .unblocked_at_phase = (unblocked_phase_value), \
      .dependency = (dependency_value), \
  }))

#define ENVY_TRACE_DEPENDENCY_ADDED(parent_value, dependency_value, needed_by_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::dependency_added{ \
      .parent = (parent_value), \
      .dependency = (dependency_value), \
      .needed_by = (needed_by_value), \
  }))

#define ENVY_TRACE_PHASE_START(spec_value, phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_start{ \
      .spec = (spec_value), \
      .phase = (phase_value), \
  }))

#define ENVY_TRACE_PHASE_COMPLETE(spec_value, phase_value, duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::phase_complete{ \
      .spec = (spec_value), \
      .phase = (phase_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_THREAD_START(spec_value, target_phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::thread_start{ \
      .spec = (spec_value), \
      .target_phase = (target_phase_value), \
  }))

#define ENVY_TRACE_THREAD_COMPLETE(spec_value, final_phase_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::thread_complete{ \
      .spec = (spec_value), \
      .final_phase = (final_phase_value), \
  }))

#define ENVY_TRACE_RECIPE_REGISTERED(spec_value, key_value, has_dependencies_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::spec_registered{ \
      .spec = (spec_value), \
      .key = (key_value), \
      .has_dependencies = (has_dependencies_value), \
  }))

#define ENVY_TRACE_TARGET_EXTENDED(spec_value, old_target_value, new_target_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::target_extended{ \
      .spec = (spec_value), \
      .old_target = (old_target_value), \
      .new_target = (new_target_value), \
  }))

#define ENVY_TRACE_LUA_CTX_RUN_START(spec_value, command_value, cwd_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_run_start{ \
      .spec = (spec_value), \
      .command = (command_value), \
      .cwd = (cwd_value), \
  }))

#define ENVY_TRACE_LUA_CTX_RUN_COMPLETE(spec_value, exit_code_value, duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_run_complete{ \
      .spec = (spec_value), \
      .exit_code = (exit_code_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_LUA_CTX_FETCH_START(spec_value, url_value, destination_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_fetch_start{ \
      .spec = (spec_value), \
      .url = (url_value), \
      .destination = (destination_value), \
  }))

#define ENVY_TRACE_LUA_CTX_FETCH_COMPLETE(spec_value, \
                                          url_value, \
                                          bytes_downloaded_value, \
                                          duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_fetch_complete{ \
      .spec = (spec_value), \
      .url = (url_value), \
      .bytes_downloaded = (bytes_downloaded_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_LUA_CTX_EXTRACT_START(spec_value, \
                                         archive_path_value, \
                                         destination_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_extract_start{ \
      .spec = (spec_value), \
      .archive_path = (archive_path_value), \
      .destination = (destination_value), \
  }))

#define ENVY_TRACE_LUA_CTX_EXTRACT_COMPLETE(spec_value, \
                                            files_extracted_value, \
                                            duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_extract_complete{ \
      .spec = (spec_value), \
      .files_extracted = (files_extracted_value), \
      .duration_ms = (duration_value), \
  }))

#define ENVY_TRACE_LUA_CTX_PACKAGE_ACCESS(spec_value, \
                                          target_value, \
                                          current_phase_value, \
                                          needed_by_value, \
                                          allowed_value, \
                                          reason_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_package_access{ \
      .spec = (spec_value), \
      .target = (target_value), \
      .current_phase = (current_phase_value), \
      .needed_by = (needed_by_value), \
      .allowed = (allowed_value), \
      .reason = (reason_value), \
  }))

#define ENVY_TRACE_LUA_CTX_PRODUCT_ACCESS(spec_value, \
                                          product_value, \
                                          provider_value, \
                                          current_phase_value, \
                                          needed_by_value, \
                                          allowed_value, \
                                          reason_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_product_access{ \
      .spec = (spec_value), \
      .product = (product_value), \
      .provider = (provider_value), \
      .current_phase = (current_phase_value), \
      .needed_by = (needed_by_value), \
      .allowed = (allowed_value), \
      .reason = (reason_value), \
  }))

#define ENVY_TRACE_LUA_CTX_LOADENV_SPEC_ACCESS(spec_value, \
                                               target_value, \
                                               subpath_value, \
                                               current_phase_value, \
                                               needed_by_value, \
                                               allowed_value, \
                                               reason_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lua_ctx_loadenv_spec_access{ \
      .spec = (spec_value), \
      .target = (target_value), \
      .subpath = (subpath_value), \
      .current_phase = (current_phase_value), \
      .needed_by = (needed_by_value), \
      .allowed = (allowed_value), \
      .reason = (reason_value), \
  }))

#define ENVY_TRACE_CACHE_HIT(spec_value, \
                             cache_key_value, \
                             pkg_path_value, \
                             fast_path_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::cache_hit{ \
      .spec = (spec_value), \
      .cache_key = (cache_key_value), \
      .pkg_path = (pkg_path_value), \
      .fast_path = (fast_path_value), \
  }))

#define ENVY_TRACE_CACHE_MISS(spec_value, cache_key_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::cache_miss{ \
      .spec = (spec_value), \
      .cache_key = (cache_key_value), \
  }))

#define ENVY_TRACE_LOCK_ACQUIRED(spec_value, lock_path_value, wait_duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lock_acquired{ \
      .spec = (spec_value), \
      .lock_path = (lock_path_value), \
      .wait_duration_ms = (wait_duration_value), \
  }))

#define ENVY_TRACE_LOCK_RELEASED(spec_value, lock_path_value, hold_duration_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::lock_released{ \
      .spec = (spec_value), \
      .lock_path = (lock_path_value), \
      .hold_duration_ms = (hold_duration_value), \
  }))

#define ENVY_TRACE_FETCH_FILE_START(spec_value, url_value, destination_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::fetch_file_start{ \
      .spec = (spec_value), \
      .url = (url_value), \
      .destination = (destination_value), \
  }))

#define ENVY_TRACE_FETCH_FILE_COMPLETE(spec_value, \
                                       url_value, \
                                       bytes_downloaded_value, \
                                       duration_value, \
                                       from_cache_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::fetch_file_complete{ \
      .spec = (spec_value), \
      .url = (url_value), \
      .bytes_downloaded = (bytes_downloaded_value), \
      .duration_ms = (duration_value), \
      .from_cache = (from_cache_value), \
  }))

#define ENVY_TRACE_SPEC_FETCH_COUNTER_INC(spec_value, new_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::spec_fetch_counter_inc{ \
      .spec = (spec_value), \
      .new_value = (new_value), \
  }))

#define ENVY_TRACE_SPEC_FETCH_COUNTER_DEC(spec_value, new_value, was_completed_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::spec_fetch_counter_dec{ \
      .spec = (spec_value), \
      .new_value = (new_value), \
      .was_completed = (was_completed_value), \
  }))

#define ENVY_TRACE_EXECUTE_DOWNLOADS_START(spec_value, thread_id_value, num_files_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::execute_downloads_start{ \
      .spec = (spec_value), \
      .thread_id = (thread_id_value), \
      .num_files = (num_files_value), \
  }))

#define ENVY_TRACE_EXECUTE_DOWNLOADS_COMPLETE(spec_value, \
                                              thread_id_value, \
                                              num_files_value, \
                                              duration_ms_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::execute_downloads_complete{ \
      .spec = (spec_value), \
      .thread_id = (thread_id_value), \
      .num_files = (num_files_value), \
      .duration_ms = (duration_ms_value), \
  }))

#define ENVY_TRACE_DEBUG_MARKER(spec_value, marker_id_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::debug_marker{ \
      .spec = (spec_value), \
      .marker_id = (marker_id_value), \
  }))

#define ENVY_TRACE_CACHE_CHECK_ENTRY(spec_value, entry_dir_value, check_location_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::cache_check_entry{ \
      .spec = (spec_value), \
      .entry_dir = (entry_dir_value), \
      .check_location = (check_location_value), \
  }))

#define ENVY_TRACE_CACHE_CHECK_RESULT(spec_value, \
                                      entry_dir_value, \
                                      is_complete_value, \
                                      check_location_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::cache_check_result{ \
      .spec = (spec_value), \
      .entry_dir = (entry_dir_value), \
      .is_complete = (is_complete_value), \
      .check_location = (check_location_value), \
  }))

#define ENVY_TRACE_DIRECTORY_FLUSHED(spec_value, dir_path_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::directory_flushed{ \
      .spec = (spec_value), \
      .dir_path = (dir_path_value), \
  }))

#define ENVY_TRACE_FILE_TOUCHED(spec_value, file_path_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::file_touched{ \
      .spec = (spec_value), \
      .file_path = (file_path_value), \
  }))

#define ENVY_TRACE_FILE_EXISTS_CHECK(spec_value, file_path_value, exists_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::file_exists_check{ \
      .spec = (spec_value), \
      .file_path = (file_path_value), \
      .exists = (exists_value), \
  }))

#define ENVY_TRACE_DIRECTORY_FLUSH_FAILED(spec_value, dir_path_value, reason_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::directory_flush_failed{ \
      .spec = (spec_value), \
      .dir_path = (dir_path_value), \
      .reason = (reason_value), \
  }))

#define ENVY_TRACE_EXTRACT_ARCHIVE_START(spec_value, \
                                         archive_path_value, \
                                         destination_value, \
                                         strip_components_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::extract_archive_start{ \
      .spec = (spec_value), \
      .archive_path = (archive_path_value), \
      .destination = (destination_value), \
      .strip_components = (strip_components_value), \
  }))

#define ENVY_TRACE_EXTRACT_ARCHIVE_COMPLETE(spec_value, \
                                            archive_path_value, \
                                            files_extracted_value, \
                                            duration_ms_value) \
  ENVY_TRACE_EMIT((::envy::trace_events::extract_archive_complete{ \
      .spec = (spec_value), \
      .archive_path = (archive_path_value), \
      .files_extracted = (files_extracted_value), \
      .duration_ms = (duration_ms_value), \
  }))
