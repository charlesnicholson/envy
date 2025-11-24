#include "trace.h"

#include "recipe_phase.h"
#include "util.h"

#include <sstream>
#include <string>
#include <type_traits>

namespace envy {

namespace {

std::string_view bool_string(bool value) { return value ? "true" : "false"; }

#define TRACE_NAME(type) \
  [](trace_events::type const &) -> std::string_view { return #type; }

}  // namespace

phase_trace_scope::phase_trace_scope(std::string recipe_identity,
                                     recipe_phase phase_value,
                                     std::chrono::steady_clock::time_point start_time)
    : recipe{ std::move(recipe_identity) }, phase{ phase_value }, start{ start_time } {
  ENVY_TRACE_PHASE_START(recipe, phase);
}

phase_trace_scope::~phase_trace_scope() {
  auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count() };
  ENVY_TRACE_PHASE_COMPLETE(recipe, phase, static_cast<std::int64_t>(duration_ms));
}

std::string_view trace_event_name(trace_event_t const &event) {
  return std::visit(
      match{
          TRACE_NAME(phase_blocked),
          TRACE_NAME(phase_unblocked),
          TRACE_NAME(dependency_added),
          TRACE_NAME(phase_start),
          TRACE_NAME(phase_complete),
          TRACE_NAME(thread_start),
          TRACE_NAME(thread_complete),
          TRACE_NAME(recipe_registered),
          TRACE_NAME(target_extended),
          TRACE_NAME(lua_ctx_run_start),
          TRACE_NAME(lua_ctx_run_complete),
          TRACE_NAME(lua_ctx_fetch_start),
          TRACE_NAME(lua_ctx_fetch_complete),
          TRACE_NAME(lua_ctx_extract_start),
          TRACE_NAME(lua_ctx_extract_complete),
          TRACE_NAME(cache_hit),
          TRACE_NAME(cache_miss),
          TRACE_NAME(lock_acquired),
          TRACE_NAME(lock_released),
          TRACE_NAME(fetch_file_start),
          TRACE_NAME(fetch_file_complete),
          [](auto const &) -> std::string_view { return "unknown"; },
      },
      event);
}

std::string trace_event_to_string(trace_event_t const &event) {
  return std::visit(
      match{
          [](trace_events::phase_blocked const &value) {
            std::ostringstream oss;
            oss << "phase_blocked recipe=" << value.recipe
                << " blocked_at=" << recipe_phase_name(value.blocked_at_phase)
                << " waiting_for=" << value.waiting_for
                << " target_phase=" << recipe_phase_name(value.target_phase);
            return oss.str();
          },
          [](trace_events::phase_unblocked const &value) {
            std::ostringstream oss;
            oss << "phase_unblocked recipe=" << value.recipe
                << " dependency=" << value.dependency
                << " at=" << recipe_phase_name(value.unblocked_at_phase);
            return oss.str();
          },
          [](trace_events::dependency_added const &value) {
            std::ostringstream oss;
            oss << "dependency_added parent=" << value.parent
                << " dependency=" << value.dependency
                << " needed_by=" << recipe_phase_name(value.needed_by);
            return oss.str();
          },
          [](trace_events::phase_start const &value) {
            std::ostringstream oss;
            oss << "phase_start recipe=" << value.recipe
                << " phase=" << recipe_phase_name(value.phase);
            return oss.str();
          },
          [](trace_events::phase_complete const &value) {
            std::ostringstream oss;
            oss << "phase_complete recipe=" << value.recipe
                << " phase=" << recipe_phase_name(value.phase)
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::thread_start const &value) {
            std::ostringstream oss;
            oss << "thread_start recipe=" << value.recipe
                << " target_phase=" << recipe_phase_name(value.target_phase);
            return oss.str();
          },
          [](trace_events::thread_complete const &value) {
            std::ostringstream oss;
            oss << "thread_complete recipe=" << value.recipe
                << " final_phase=" << recipe_phase_name(value.final_phase);
            return oss.str();
          },
          [](trace_events::recipe_registered const &value) {
            std::ostringstream oss;
            oss << "recipe_registered recipe=" << value.recipe << " key=" << value.key
                << " has_dependencies=" << bool_string(value.has_dependencies);
            return oss.str();
          },
          [](trace_events::target_extended const &value) {
            std::ostringstream oss;
            oss << "target_extended recipe=" << value.recipe
                << " old_target=" << recipe_phase_name(value.old_target)
                << " new_target=" << recipe_phase_name(value.new_target);
            return oss.str();
          },
          [](trace_events::lua_ctx_run_start const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_run_start recipe=" << value.recipe
                << " command=" << value.command << " cwd=" << value.cwd;
            return oss.str();
          },
          [](trace_events::lua_ctx_run_complete const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_run_complete recipe=" << value.recipe
                << " exit_code=" << value.exit_code
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::lua_ctx_fetch_start const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_fetch_start recipe=" << value.recipe << " url=" << value.url
                << " destination=" << value.destination;
            return oss.str();
          },
          [](trace_events::lua_ctx_fetch_complete const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_fetch_complete recipe=" << value.recipe << " url=" << value.url
                << " bytes_downloaded=" << value.bytes_downloaded
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::lua_ctx_extract_start const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_extract_start recipe=" << value.recipe
                << " archive_path=" << value.archive_path
                << " destination=" << value.destination;
            return oss.str();
          },
          [](trace_events::lua_ctx_extract_complete const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_extract_complete recipe=" << value.recipe
                << " files_extracted=" << value.files_extracted
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::cache_hit const &value) {
            std::ostringstream oss;
            oss << "cache_hit recipe=" << value.recipe << " cache_key=" << value.cache_key
                << " asset_path=" << value.asset_path;
            return oss.str();
          },
          [](trace_events::cache_miss const &value) {
            std::ostringstream oss;
            oss << "cache_miss recipe=" << value.recipe
                << " cache_key=" << value.cache_key;
            return oss.str();
          },
          [](trace_events::lock_acquired const &value) {
            std::ostringstream oss;
            oss << "lock_acquired recipe=" << value.recipe
                << " lock_path=" << value.lock_path
                << " wait_ms=" << value.wait_duration_ms;
            return oss.str();
          },
          [](trace_events::lock_released const &value) {
            std::ostringstream oss;
            oss << "lock_released recipe=" << value.recipe
                << " lock_path=" << value.lock_path
                << " hold_ms=" << value.hold_duration_ms;
            return oss.str();
          },
          [](trace_events::fetch_file_start const &value) {
            std::ostringstream oss;
            oss << "fetch_file_start recipe=" << value.recipe << " url=" << value.url
                << " destination=" << value.destination;
            return oss.str();
          },
          [](trace_events::fetch_file_complete const &value) {
            std::ostringstream oss;
            oss << "fetch_file_complete recipe=" << value.recipe << " url=" << value.url
                << " bytes_downloaded=" << value.bytes_downloaded
                << " duration_ms=" << value.duration_ms
                << " from_cache=" << bool_string(value.from_cache);
            return oss.str();
          },
          [](auto const &) {
            std::ostringstream oss;
            oss << "trace_event_unknown";
            return oss.str();
          },
      },
      event);
}

}  // namespace envy
