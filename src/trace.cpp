#include "trace.h"

#include "pkg_phase.h"
#include "util.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
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

  std::ostringstream oss;
  oss << base << '.' << std::setfill('0') << std::setw(3) << millis << 'Z';
  return oss.str();
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
          static constexpr char hex[] = "0123456789abcdef";
          out.append("\\u00");
          out.push_back(hex[(ch >> 4) & 0xF]);
          out.push_back(hex[ch & 0xF]);
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

void append_phase(std::string &out, char const *key, pkg_phase phase) {
  append_kv(out, key, pkg_phase_name(phase));
  std::string number_key = std::string(key) + "_num";
  append_kv(out, number_key.c_str(), static_cast<std::int64_t>(static_cast<int>(phase)));
}

}  // namespace

phase_trace_scope::phase_trace_scope(std::string pkg_identity,
                                     pkg_phase phase_value,
                                     std::chrono::steady_clock::time_point start_time)
    : spec{ std::move(pkg_identity) }, phase{ phase_value }, start{ start_time } {
  ENVY_TRACE_PHASE_START(spec, phase);
}

phase_trace_scope::~phase_trace_scope() {
  auto const duration_ms{ std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count() };
  ENVY_TRACE_PHASE_COMPLETE(spec, phase, static_cast<std::int64_t>(duration_ms));
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
          TRACE_NAME(spec_registered),
          TRACE_NAME(target_extended),
          TRACE_NAME(lua_ctx_run_start),
          TRACE_NAME(lua_ctx_run_complete),
          TRACE_NAME(lua_ctx_fetch_start),
          TRACE_NAME(lua_ctx_fetch_complete),
          TRACE_NAME(lua_ctx_extract_start),
          TRACE_NAME(lua_ctx_extract_complete),
          TRACE_NAME(lua_ctx_package_access),
          TRACE_NAME(lua_ctx_product_access),
          TRACE_NAME(lua_ctx_loadenv_spec_access),
          TRACE_NAME(cache_hit),
          TRACE_NAME(cache_miss),
          TRACE_NAME(lock_acquired),
          TRACE_NAME(lock_released),
          TRACE_NAME(fetch_file_start),
          TRACE_NAME(fetch_file_complete),
          TRACE_NAME(spec_fetch_counter_inc),
          TRACE_NAME(spec_fetch_counter_dec),
          TRACE_NAME(debug_marker),
          TRACE_NAME(cache_check_entry),
          TRACE_NAME(cache_check_result),
          TRACE_NAME(directory_flushed),
          TRACE_NAME(file_touched),
          TRACE_NAME(file_exists_check),
          TRACE_NAME(directory_flush_failed),
          TRACE_NAME(extract_archive_start),
          TRACE_NAME(extract_archive_complete),
          TRACE_NAME(product_transitive_check),
          TRACE_NAME(product_transitive_check_dep),
          TRACE_NAME(product_parsed),
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
            oss << "phase_blocked spec=" << value.spec
                << " blocked_at=" << pkg_phase_name(value.blocked_at_phase)
                << " waiting_for=" << value.waiting_for
                << " target_phase=" << pkg_phase_name(value.target_phase);
            return oss.str();
          },
          [](trace_events::phase_unblocked const &value) {
            std::ostringstream oss;
            oss << "phase_unblocked spec=" << value.spec
                << " dependency=" << value.dependency
                << " at=" << pkg_phase_name(value.unblocked_at_phase);
            return oss.str();
          },
          [](trace_events::dependency_added const &value) {
            std::ostringstream oss;
            oss << "dependency_added parent=" << value.parent
                << " dependency=" << value.dependency
                << " needed_by=" << pkg_phase_name(value.needed_by);
            return oss.str();
          },
          [](trace_events::phase_start const &value) {
            std::ostringstream oss;
            oss << "phase_start spec=" << value.spec
                << " phase=" << pkg_phase_name(value.phase);
            return oss.str();
          },
          [](trace_events::phase_complete const &value) {
            std::ostringstream oss;
            oss << "phase_complete spec=" << value.spec
                << " phase=" << pkg_phase_name(value.phase)
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::thread_start const &value) {
            std::ostringstream oss;
            oss << "thread_start spec=" << value.spec
                << " target_phase=" << pkg_phase_name(value.target_phase);
            return oss.str();
          },
          [](trace_events::thread_complete const &value) {
            std::ostringstream oss;
            oss << "thread_complete spec=" << value.spec
                << " final_phase=" << pkg_phase_name(value.final_phase);
            return oss.str();
          },
          [](trace_events::spec_registered const &value) {
            std::ostringstream oss;
            oss << "spec_registered spec=" << value.spec << " key=" << value.key
                << " has_dependencies=" << bool_string(value.has_dependencies);
            return oss.str();
          },
          [](trace_events::target_extended const &value) {
            std::ostringstream oss;
            oss << "target_extended spec=" << value.spec
                << " old_target=" << pkg_phase_name(value.old_target)
                << " new_target=" << pkg_phase_name(value.new_target);
            return oss.str();
          },
          [](trace_events::lua_ctx_run_start const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_run_start spec=" << value.spec << " command=" << value.command
                << " cwd=" << value.cwd;
            return oss.str();
          },
          [](trace_events::lua_ctx_run_complete const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_run_complete spec=" << value.spec
                << " exit_code=" << value.exit_code
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::lua_ctx_fetch_start const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_fetch_start spec=" << value.spec << " url=" << value.url
                << " destination=" << value.destination;
            return oss.str();
          },
          [](trace_events::lua_ctx_fetch_complete const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_fetch_complete spec=" << value.spec << " url=" << value.url
                << " bytes_downloaded=" << value.bytes_downloaded
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::lua_ctx_extract_start const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_extract_start spec=" << value.spec
                << " archive_path=" << value.archive_path
                << " destination=" << value.destination;
            return oss.str();
          },
          [](trace_events::lua_ctx_extract_complete const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_extract_complete spec=" << value.spec
                << " files_extracted=" << value.files_extracted
                << " duration_ms=" << value.duration_ms;
            return oss.str();
          },
          [](trace_events::lua_ctx_package_access const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_package_access spec=" << value.spec
                << " target=" << value.target
                << " current_phase=" << pkg_phase_name(value.current_phase)
                << " needed_by=" << pkg_phase_name(value.needed_by)
                << " allowed=" << bool_string(value.allowed) << " reason=" << value.reason;
            return oss.str();
          },
          [](trace_events::lua_ctx_product_access const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_product_access spec=" << value.spec
                << " product=" << value.product << " provider=" << value.provider
                << " current_phase=" << pkg_phase_name(value.current_phase)
                << " needed_by=" << pkg_phase_name(value.needed_by)
                << " allowed=" << bool_string(value.allowed) << " reason=" << value.reason;
            return oss.str();
          },
          [](trace_events::lua_ctx_loadenv_spec_access const &value) {
            std::ostringstream oss;
            oss << "lua_ctx_loadenv_spec_access spec=" << value.spec
                << " target=" << value.target << " subpath=" << value.subpath
                << " current_phase=" << pkg_phase_name(value.current_phase)
                << " needed_by=" << pkg_phase_name(value.needed_by)
                << " allowed=" << bool_string(value.allowed) << " reason=" << value.reason;
            return oss.str();
          },
          [](trace_events::cache_hit const &value) {
            std::ostringstream oss;
            oss << "cache_hit spec=" << value.spec << " cache_key=" << value.cache_key
                << " pkg_path=" << value.pkg_path
                << " fast_path=" << (value.fast_path ? "true" : "false");
            return oss.str();
          },
          [](trace_events::cache_miss const &value) {
            std::ostringstream oss;
            oss << "cache_miss spec=" << value.spec << " cache_key=" << value.cache_key;
            return oss.str();
          },
          [](trace_events::lock_acquired const &value) {
            std::ostringstream oss;
            oss << "lock_acquired spec=" << value.spec << " lock_path=" << value.lock_path
                << " wait_ms=" << value.wait_duration_ms;
            return oss.str();
          },
          [](trace_events::lock_released const &value) {
            std::ostringstream oss;
            oss << "lock_released spec=" << value.spec << " lock_path=" << value.lock_path
                << " hold_ms=" << value.hold_duration_ms;
            return oss.str();
          },
          [](trace_events::fetch_file_start const &value) {
            std::ostringstream oss;
            oss << "fetch_file_start spec=" << value.spec << " url=" << value.url
                << " destination=" << value.destination;
            return oss.str();
          },
          [](trace_events::fetch_file_complete const &value) {
            std::ostringstream oss;
            oss << "fetch_file_complete spec=" << value.spec << " url=" << value.url
                << " bytes_downloaded=" << value.bytes_downloaded
                << " duration_ms=" << value.duration_ms
                << " from_cache=" << bool_string(value.from_cache);
            return oss.str();
          },
          [](trace_events::spec_fetch_counter_inc const &value) {
            std::ostringstream oss;
            oss << "spec_fetch_counter_inc spec=" << value.spec
                << " new_value=" << value.new_value;
            return oss.str();
          },
          [](trace_events::spec_fetch_counter_dec const &value) {
            std::ostringstream oss;
            oss << "spec_fetch_counter_dec spec=" << value.spec
                << " new_value=" << value.new_value
                << " was_completed=" << bool_string(value.was_completed);
            return oss.str();
          },
          [](trace_events::debug_marker const &value) {
            std::ostringstream oss;
            oss << "debug_marker spec=" << value.spec << " marker_id=" << value.marker_id;
            return oss.str();
          },
          [](trace_events::cache_check_entry const &value) {
            std::ostringstream oss;
            oss << "cache_check_entry spec=" << value.spec
                << " entry_dir=" << value.entry_dir
                << " check_location=" << value.check_location;
            return oss.str();
          },
          [](trace_events::cache_check_result const &value) {
            std::ostringstream oss;
            oss << "cache_check_result spec=" << value.spec
                << " entry_dir=" << value.entry_dir
                << " is_complete=" << bool_string(value.is_complete)
                << " check_location=" << value.check_location;
            return oss.str();
          },
          [](trace_events::directory_flushed const &value) {
            std::ostringstream oss;
            oss << "directory_flushed spec=" << value.spec
                << " dir_path=" << value.dir_path;
            return oss.str();
          },
          [](trace_events::file_touched const &value) {
            std::ostringstream oss;
            oss << "file_touched spec=" << value.spec << " file_path=" << value.file_path;
            return oss.str();
          },
          [](trace_events::file_exists_check const &value) {
            std::ostringstream oss;
            oss << "file_exists_check spec=" << value.spec
                << " file_path=" << value.file_path
                << " exists=" << bool_string(value.exists);
            return oss.str();
          },
          [](trace_events::directory_flush_failed const &value) {
            std::ostringstream oss;
            oss << "directory_flush_failed spec=" << value.spec
                << " dir_path=" << value.dir_path << " reason=" << value.reason;
            return oss.str();
          },
          [](trace_events::extract_archive_start const &value) {
            std::ostringstream oss;
            oss << "extract_archive_start spec=" << value.spec
                << " archive_path=" << value.archive_path
                << " destination=" << value.destination
                << " strip_components=" << value.strip_components;
            return oss.str();
          },
          [](trace_events::extract_archive_complete const &value) {
            std::ostringstream oss;
            oss << "extract_archive_complete spec=" << value.spec
                << " archive_path=" << value.archive_path
                << " files_extracted=" << value.files_extracted
                << " duration_ms=" << value.duration_ms;
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

  auto const append_spec{ [&](std::string_view value) {
    append_kv(output, "spec", value);
  } };

  std::visit(
      match{
          [&](trace_events::phase_blocked const &value) {
            append_spec(value.spec);
            append_phase(output, "blocked_at_phase", value.blocked_at_phase);
            append_kv(output, "waiting_for", value.waiting_for);
            append_phase(output, "target_phase", value.target_phase);
          },
          [&](trace_events::phase_unblocked const &value) {
            append_spec(value.spec);
            append_phase(output, "unblocked_at_phase", value.unblocked_at_phase);
            append_kv(output, "dependency", value.dependency);
          },
          [&](trace_events::dependency_added const &value) {
            append_kv(output, "parent", value.parent);
            append_kv(output, "dependency", value.dependency);
            append_phase(output, "needed_by", value.needed_by);
          },
          [&](trace_events::phase_start const &value) {
            append_spec(value.spec);
            append_phase(output, "phase", value.phase);
          },
          [&](trace_events::phase_complete const &value) {
            append_spec(value.spec);
            append_phase(output, "phase", value.phase);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::thread_start const &value) {
            append_spec(value.spec);
            append_phase(output, "target_phase", value.target_phase);
          },
          [&](trace_events::thread_complete const &value) {
            append_spec(value.spec);
            append_phase(output, "final_phase", value.final_phase);
          },
          [&](trace_events::spec_registered const &value) {
            append_spec(value.spec);
            append_kv(output, "key", value.key);
            append_kv(output, "has_dependencies", value.has_dependencies);
          },
          [&](trace_events::target_extended const &value) {
            append_spec(value.spec);
            append_phase(output, "old_target", value.old_target);
            append_phase(output, "new_target", value.new_target);
          },
          [&](trace_events::lua_ctx_run_start const &value) {
            append_spec(value.spec);
            append_kv(output, "command", value.command);
            append_kv(output, "cwd", value.cwd);
          },
          [&](trace_events::lua_ctx_run_complete const &value) {
            append_spec(value.spec);
            append_kv(output, "exit_code", static_cast<std::int64_t>(value.exit_code));
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::lua_ctx_fetch_start const &value) {
            append_spec(value.spec);
            append_kv(output, "url", value.url);
            append_kv(output, "destination", value.destination);
          },
          [&](trace_events::lua_ctx_fetch_complete const &value) {
            append_spec(value.spec);
            append_kv(output, "url", value.url);
            append_kv(output, "bytes_downloaded", value.bytes_downloaded);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::lua_ctx_extract_start const &value) {
            append_spec(value.spec);
            append_kv(output, "archive_path", value.archive_path);
            append_kv(output, "destination", value.destination);
          },
          [&](trace_events::lua_ctx_extract_complete const &value) {
            append_spec(value.spec);
            append_kv(output, "files_extracted", value.files_extracted);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
          },
          [&](trace_events::lua_ctx_package_access const &value) {
            append_spec(value.spec);
            append_kv(output, "target", value.target);
            append_phase(output, "current_phase", value.current_phase);
            append_phase(output, "needed_by", value.needed_by);
            append_kv(output, "allowed", value.allowed);
            append_kv(output, "reason", value.reason);
          },
          [&](trace_events::lua_ctx_product_access const &value) {
            append_spec(value.spec);
            append_kv(output, "product", value.product);
            append_kv(output, "provider", value.provider);
            append_phase(output, "current_phase", value.current_phase);
            append_phase(output, "needed_by", value.needed_by);
            append_kv(output, "allowed", value.allowed);
            append_kv(output, "reason", value.reason);
          },
          [&](trace_events::lua_ctx_loadenv_spec_access const &value) {
            append_spec(value.spec);
            append_kv(output, "target", value.target);
            append_kv(output, "subpath", value.subpath);
            append_phase(output, "current_phase", value.current_phase);
            append_phase(output, "needed_by", value.needed_by);
            append_kv(output, "allowed", value.allowed);
            append_kv(output, "reason", value.reason);
          },
          [&](trace_events::cache_hit const &value) {
            append_spec(value.spec);
            append_kv(output, "cache_key", value.cache_key);
            append_kv(output, "pkg_path", value.pkg_path);
            append_kv(output, "fast_path", value.fast_path);
          },
          [&](trace_events::cache_miss const &value) {
            append_spec(value.spec);
            append_kv(output, "cache_key", value.cache_key);
          },
          [&](trace_events::lock_acquired const &value) {
            append_spec(value.spec);
            append_kv(output, "lock_path", value.lock_path);
            append_kv(output, "wait_duration_ms", value.wait_duration_ms);
          },
          [&](trace_events::lock_released const &value) {
            append_spec(value.spec);
            append_kv(output, "lock_path", value.lock_path);
            append_kv(output, "hold_duration_ms", value.hold_duration_ms);
          },
          [&](trace_events::fetch_file_start const &value) {
            append_spec(value.spec);
            append_kv(output, "url", value.url);
            append_kv(output, "destination", value.destination);
          },
          [&](trace_events::fetch_file_complete const &value) {
            append_spec(value.spec);
            append_kv(output, "url", value.url);
            append_kv(output, "bytes_downloaded", value.bytes_downloaded);
            append_kv(output, "duration_ms", static_cast<std::int64_t>(value.duration_ms));
            append_kv(output, "from_cache", value.from_cache);
          },
          [&](trace_events::spec_fetch_counter_inc const &value) {
            append_spec(value.spec);
            append_kv(output, "new_value", static_cast<std::int64_t>(value.new_value));
          },
          [&](trace_events::spec_fetch_counter_dec const &value) {
            append_spec(value.spec);
            append_kv(output, "new_value", static_cast<std::int64_t>(value.new_value));
            append_kv(output, "was_completed", value.was_completed);
          },
          [&](trace_events::debug_marker const &value) {
            append_spec(value.spec);
            append_kv(output, "marker_id", static_cast<std::int64_t>(value.marker_id));
          },
          [&](trace_events::cache_check_entry const &value) {
            append_spec(value.spec);
            append_kv(output, "entry_dir", value.entry_dir);
            append_kv(output, "check_location", value.check_location);
          },
          [&](trace_events::cache_check_result const &value) {
            append_spec(value.spec);
            append_kv(output, "entry_dir", value.entry_dir);
            append_kv(output, "is_complete", value.is_complete);
            append_kv(output, "check_location", value.check_location);
          },
          [&](trace_events::directory_flushed const &value) {
            append_spec(value.spec);
            append_kv(output, "dir_path", value.dir_path);
          },
          [&](trace_events::file_touched const &value) {
            append_spec(value.spec);
            append_kv(output, "file_path", value.file_path);
          },
          [&](trace_events::file_exists_check const &value) {
            append_spec(value.spec);
            append_kv(output, "file_path", value.file_path);
            append_kv(output, "exists", value.exists);
          },
          [&](trace_events::directory_flush_failed const &value) {
            append_spec(value.spec);
            append_kv(output, "dir_path", value.dir_path);
            append_kv(output, "reason", value.reason);
          },
          [&](trace_events::extract_archive_start const &value) {
            append_spec(value.spec);
            append_kv(output, "archive_path", value.archive_path);
            append_kv(output, "destination", value.destination);
            append_kv(output,
                      "strip_components",
                      static_cast<std::int64_t>(value.strip_components));
          },
          [&](trace_events::extract_archive_complete const &value) {
            append_spec(value.spec);
            append_kv(output, "archive_path", value.archive_path);
            append_kv(output, "files_extracted", value.files_extracted);
            append_kv(output, "duration_ms", value.duration_ms);
          },
          [&](trace_events::product_transitive_check const &value) {
            append_spec(value.spec);
            append_kv(output, "product", value.product);
            append_kv(output, "has_product_directly", value.has_product_directly);
            append_kv(output,
                      "dependency_count",
                      static_cast<std::int64_t>(value.dependency_count));
          },
          [&](trace_events::product_transitive_check_dep const &value) {
            append_spec(value.spec);
            append_kv(output, "product", value.product);
            append_kv(output, "checking_dependency", value.checking_dependency);
          },
          [&](trace_events::product_parsed const &value) {
            append_spec(value.spec);
            append_kv(output, "product_name", value.product_name);
            append_kv(output, "product_value", value.product_value);
          },
          [](auto const &) {},
      },
      event);

  output.push_back('}');
  return output;
}

}  // namespace envy
