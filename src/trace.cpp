#include "trace.h"

#include "recipe_phase.h"
#include "util.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>

namespace envy {

namespace {

std::string_view bool_string(bool value) { return value ? "true" : "false"; }

std::tm make_utc_tm(std::time_t time) {
  std::tm result{};

#if defined(_WIN32)
  gmtime_s(&result, &time);
#else
  gmtime_r(&time, &result);
#endif

  return result;
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
  auto const seconds{ std::chrono::time_point_cast<std::chrono::seconds>(tp) };
  auto const millis{
    std::chrono::duration_cast<std::chrono::milliseconds>(tp - seconds).count()
  };

  std::time_t const timestamp{ std::chrono::system_clock::to_time_t(seconds) };
  std::tm const utc_tm{ make_utc_tm(timestamp) };

  char base[32]{};
  if (std::strftime(base, sizeof base, "%Y-%m-%dT%H:%M:%S", &utc_tm) == 0) { return {}; }

  char buffer[64]{};
  int const written{ std::snprintf(buffer,
                                   sizeof buffer,
                                   "%s.%03lldZ",
                                   base,
                                   static_cast<long long>(millis)) };
  if (written <= 0) { return {}; }

  return std::string{ buffer, static_cast<std::size_t>(written) };
}

void append_json_string(std::string &out, std::string_view value) {
  for (char const ch : value) {
    switch (ch) {
      case '\\': out.append("\\\\"); break;
      case '"': out.append("\\\""); break;
      case '\b': out.append("\\b"); break;
      case '\f': out.append("\\f"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char escape[7]{};
          std::snprintf(escape,
                        sizeof escape,
                        "\\u%04x",
                        static_cast<unsigned int>(static_cast<unsigned char>(ch)));
          out.append(escape);
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
}

void append_kv(std::string &out, char const *key, std::string_view value) {
  out.push_back(',');
  out.push_back('"');
  out.append(key);
  out.append("\":\"");
  append_json_string(out, value);
  out.push_back('"');
}

void append_kv(std::string &out, char const *key, std::int64_t value) {
  out.push_back(',');
  out.push_back('"');
  out.append(key);
  out.append("\":");
  out.append(std::to_string(value));
}

void append_kv(std::string &out, char const *key, bool value) {
  out.push_back(',');
  out.push_back('"');
  out.append(key);
  out.append("\":");
  out.append(value ? "true" : "false");
}

void append_phase(std::string &out, char const *key, recipe_phase phase) {
  append_kv(out, key, recipe_phase_name(phase));

  char number_key[64]{};
  std::snprintf(number_key, sizeof number_key, "%s_num", key);
  append_kv(out, number_key, static_cast<std::int64_t>(static_cast<int>(phase)));
}

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

#define TRACE_NAME(type) \
  [](trace_events::type const &) -> std::string_view { return #type; }

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
          TRACE_NAME(lua_ctx_asset_access),
          TRACE_NAME(lua_ctx_product_access),
          TRACE_NAME(cache_hit),
          TRACE_NAME(cache_miss),
          TRACE_NAME(lock_acquired),
          TRACE_NAME(lock_released),
          TRACE_NAME(fetch_file_start),
          TRACE_NAME(fetch_file_complete),
          TRACE_NAME(recipe_fetch_counter_inc),
          TRACE_NAME(recipe_fetch_counter_dec),
          TRACE_NAME(debug_marker),
          [](auto const &) -> std::string_view { return "unknown"; },
      },
      event);
}

#undef TRACE_NAME

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
          [](trace_events::lua_ctx_asset_access const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_asset_access recipe=" << value.recipe
                << " target=" << value.target
                << " current_phase=" << recipe_phase_name(value.current_phase)
                << " needed_by=" << recipe_phase_name(value.needed_by)
                << " allowed=" << bool_string(value.allowed) << " reason=" << value.reason;
            return oss.str();
          },
          [](trace_events::lua_ctx_product_access const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_product_access recipe=" << value.recipe
                << " product=" << value.product << " provider=" << value.provider
                << " current_phase=" << recipe_phase_name(value.current_phase)
                << " needed_by=" << recipe_phase_name(value.needed_by)
                << " allowed=" << bool_string(value.allowed) << " reason=" << value.reason;
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
          [](trace_events::recipe_fetch_counter_inc const &value) {
            std::ostringstream oss;
            oss << "recipe_fetch_counter_inc recipe=" << value.recipe
                << " new_value=" << value.new_value;
            return oss.str();
          },
          [](trace_events::recipe_fetch_counter_dec const &value) {
            std::ostringstream oss;
            oss << "recipe_fetch_counter_dec recipe=" << value.recipe
                << " new_value=" << value.new_value
                << " was_completed=" << bool_string(value.was_completed);
            return oss.str();
          },
          [](trace_events::debug_marker const &value) {
            std::ostringstream oss;
            oss << "debug_marker recipe=" << value.recipe
                << " marker_id=" << value.marker_id;
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

std::string trace_event_to_json(trace_event_t const &event) {
  std::string output;
  output.reserve(256);

  output.append("{\"ts\":\"");
  output.append(format_timestamp(std::chrono::system_clock::now()));
  output.append("\",\"event\":\"");
  output.append(trace_event_name(event));
  output.push_back('"');

  auto const append_recipe{ [&](std::string_view value) {
    append_kv(output, "recipe", value);
  } };

  std::visit(
      match{
          [&](trace_events::phase_blocked const &value) {
            append_recipe(value.recipe);
            append_phase(output, "blocked_at_phase", value.blocked_at_phase);
            append_kv(output, "waiting_for", value.waiting_for);
            append_phase(output, "target_phase", value.target_phase);
          },
          [&](trace_events::phase_unblocked const &value) {
            append_recipe(value.recipe);
            append_phase(output, "unblocked_at_phase", value.unblocked_at_phase);
            append_kv(output, "dependency", value.dependency);
          },
          [&](trace_events::dependency_added const &value) {
            append_kv(output, "parent", value.parent);
            append_kv(output, "dependency", value.dependency);
            append_phase(output, "needed_by", value.needed_by);
          },
          [&](trace_events::phase_start const &value) {
            append_recipe(value.recipe);
            append_phase(output, "phase", value.phase);
          },
          [&](trace_events::phase_complete const &value) {
            append_recipe(value.recipe);
            append_phase(output, "phase", value.phase);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::thread_start const &value) {
            append_recipe(value.recipe);
            append_phase(output, "target_phase", value.target_phase);
          },
          [&](trace_events::thread_complete const &value) {
            append_recipe(value.recipe);
            append_phase(output, "final_phase", value.final_phase);
          },
          [&](trace_events::recipe_registered const &value) {
            append_recipe(value.recipe);
            append_kv(output, "key", value.key);
            append_kv(output, "has_dependencies", value.has_dependencies);
          },
          [&](trace_events::target_extended const &value) {
            append_recipe(value.recipe);
            append_phase(output, "old_target", value.old_target);
            append_phase(output, "new_target", value.new_target);
          },
          [&](trace_events::lua_ctx_run_start const &value) {
            append_recipe(value.recipe);
            append_kv(output, "command", value.command);
            append_kv(output, "cwd", value.cwd);
          },
          [&](trace_events::lua_ctx_run_complete const &value) {
            append_recipe(value.recipe);
            append_kv(output, "exit_code", static_cast<std::int64_t>(value.exit_code));
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::lua_ctx_fetch_start const &value) {
            append_recipe(value.recipe);
            append_kv(output, "url", value.url);
            append_kv(output, "destination", value.destination);
          },
          [&](trace_events::lua_ctx_fetch_complete const &value) {
            append_recipe(value.recipe);
            append_kv(output, "url", value.url);
            append_kv(output, "bytes_downloaded", value.bytes_downloaded);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::lua_ctx_extract_start const &value) {
            append_recipe(value.recipe);
            append_kv(output, "archive_path", value.archive_path);
            append_kv(output, "destination", value.destination);
          },
          [&](trace_events::lua_ctx_extract_complete const &value) {
            append_recipe(value.recipe);
            append_kv(output, "files_extracted", value.files_extracted);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::lua_ctx_asset_access const &value) {
            append_recipe(value.recipe);
            append_kv(output, "target", value.target);
            append_phase(output, "current_phase", value.current_phase);
            append_phase(output, "needed_by", value.needed_by);
            append_kv(output, "allowed", value.allowed);
            append_kv(output, "reason", value.reason);
          },
          [&](trace_events::lua_ctx_product_access const &value) {
            append_recipe(value.recipe);
            append_kv(output, "product", value.product);
            append_kv(output, "provider", value.provider);
            append_phase(output, "current_phase", value.current_phase);
            append_phase(output, "needed_by", value.needed_by);
            append_kv(output, "allowed", value.allowed);
            append_kv(output, "reason", value.reason);
          },
          [&](trace_events::cache_hit const &value) {
            append_recipe(value.recipe);
            append_kv(output, "cache_key", value.cache_key);
            append_kv(output, "asset_path", value.asset_path);
          },
          [&](trace_events::cache_miss const &value) {
            append_recipe(value.recipe);
            append_kv(output, "cache_key", value.cache_key);
          },
          [&](trace_events::lock_acquired const &value) {
            append_recipe(value.recipe);
            append_kv(output, "lock_path", value.lock_path);
            append_kv(output, "wait_duration_ms", value.wait_duration_ms);
          },
          [&](trace_events::lock_released const &value) {
            append_recipe(value.recipe);
            append_kv(output, "lock_path", value.lock_path);
            append_kv(output, "hold_duration_ms", value.hold_duration_ms);
          },
          [&](trace_events::fetch_file_start const &value) {
            append_recipe(value.recipe);
            append_kv(output, "url", value.url);
            append_kv(output, "destination", value.destination);
          },
          [&](trace_events::fetch_file_complete const &value) {
            append_recipe(value.recipe);
            append_kv(output, "url", value.url);
            append_kv(output, "bytes_downloaded", value.bytes_downloaded);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
            append_kv(output, "from_cache", value.from_cache);
          },
          [&](trace_events::recipe_fetch_counter_inc const &value) {
            append_recipe(value.recipe);
            append_kv(output, "new_value", static_cast<std::int64_t>(value.new_value));
          },
          [&](trace_events::recipe_fetch_counter_dec const &value) {
            append_recipe(value.recipe);
            append_kv(output, "new_value", static_cast<std::int64_t>(value.new_value));
            append_kv(output, "was_completed", value.was_completed);
          },
          [&](trace_events::debug_marker const &value) {
            append_recipe(value.recipe);
            append_kv(output, "marker_id", static_cast<std::int64_t>(value.marker_id));
          },
          [](auto const &) {},
      },
      event);

  output.push_back('}');
  return output;
}

}  // namespace envy
