#include "phase_fetch.h"

#include "cache.h"
#include "engine.h"
#include "fetch.h"
#include "lua_ctx/lua_ctx_bindings.h"
#include "lua_envy.h"
#include "lua_error_formatter.h"
#include "recipe.h"
#include "sha256.h"
#include "sol_util.h"
#include "trace.h"
#include "tui.h"
#include "uri.h"
#include "util.h"
#ifdef ENVY_FUNCTIONAL_TESTER
#include "test_support.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace envy {

// Create fetch_request from URL and destination, validating scheme
// Not in anonymous namespace so tests can call it
fetch_request url_to_fetch_request(std::string const &url,
                                   std::filesystem::path const &dest,
                                   std::optional<std::string> const &ref,
                                   std::string const &context) {
  auto const info{ uri_classify(url) };

  switch (info.scheme) {
    case uri_scheme::HTTP: return fetch_request_http{ .source = url, .destination = dest };
    case uri_scheme::HTTPS:
      return fetch_request_https{ .source = url, .destination = dest };
    case uri_scheme::FTP: return fetch_request_ftp{ .source = url, .destination = dest };
    case uri_scheme::FTPS: return fetch_request_ftps{ .source = url, .destination = dest };
    case uri_scheme::S3:
      return fetch_request_s3{ .source = url, .destination = dest };
      // TODO: region support
    case uri_scheme::LOCAL_FILE_ABSOLUTE:
    case uri_scheme::LOCAL_FILE_RELATIVE:
      return fetch_request_file{ .source = url, .destination = dest };
      // TODO: file_root support
    case uri_scheme::GIT:
      if (!ref.has_value() || ref->empty()) {
        throw std::runtime_error("Git URLs require 'ref' field in " + context);
      }
      return fetch_request_git{ .source = url, .destination = dest, .ref = *ref };
    default: throw std::runtime_error("Unsupported URL scheme in " + context + ": " + url);
  }
}

namespace {

// Helper to extract destination from fetch_request variant
std::filesystem::path const &get_destination(fetch_request const &req) {
  return std::visit(
      [](auto const &r) -> std::filesystem::path const & { return r.destination; },
      req);
}

// Helper to extract source from fetch_request variant
std::string const &get_source(fetch_request const &req) {
  return std::visit([](auto const &r) -> std::string const & { return r.source; }, req);
}

struct fetch_spec {
  fetch_request request;
  std::string sha256;
};

struct table_entry {
  std::string url;
  std::string sha256;
  std::optional<std::string> ref;
};

std::vector<fetch_spec> parse_fetch_field(sol::object const &fetch_obj,
                                          std::filesystem::path const &fetch_dir,
                                          std::filesystem::path const &stage_dir,
                                          std::string const &key);
std::vector<size_t> determine_downloads_needed(std::vector<fetch_spec> const &specs);
void execute_downloads(std::vector<fetch_spec> const &specs,
                       std::vector<size_t> const &to_download_indices,
                       std::string const &key,
                       tui::section_handle section);

bool run_programmatic_fetch(sol::protected_function fetch_func,
                            cache::scoped_entry_lock *lock,
                            std::string const &identity,
                            engine &eng,
                            recipe *r) {
  tui::debug("phase fetch: executing fetch function");

  // Build context (inherits from lua_ctx_common)
  fetch_phase_ctx ctx{};
  ctx.fetch_dir = lock->fetch_dir();
  ctx.run_dir = lock->tmp_dir();
  ctx.stage_dir = lock->stage_dir();
  ctx.engine_ = &eng;
  ctx.recipe_ = r;
  ctx.used_basenames = {};

  sol::state_view lua{ fetch_func.lua_state() };
  sol::table ctx_table{ build_fetch_phase_ctx_table(lua, identity, &ctx) };

  // Get options from registry and pass as 2nd arg
  sol::object opts{ lua.registry()[ENVY_OPTIONS_RIDX] };

  sol::protected_function_result result{ call_lua_function_with_enriched_errors(
      r,
      "FETCH",
      [&]() { return fetch_func(ctx_table, opts); }) };

  bool should_mark_complete{ true };
  sol::object return_value{ result };

  if (return_value.get_type() == sol::type::none ||
      return_value.get_type() == sol::type::lua_nil) {
    tui::debug("phase fetch: function returned nil, imperative mode only");
  } else if (return_value.is<std::string>() || return_value.is<sol::table>()) {
    tui::debug("phase fetch: function returned declarative spec, processing");

    auto const fetch_specs{
      parse_fetch_field(return_value, lock->fetch_dir(), lock->stage_dir(), identity)
    };

    if (!fetch_specs.empty()) {
      auto const to_download{ determine_downloads_needed(fetch_specs) };
      auto const tid{ std::hash<std::thread::id>{}(std::this_thread::get_id()) };
      auto const start{ std::chrono::steady_clock::now() };

      ENVY_TRACE_EXECUTE_DOWNLOADS_START(identity, tid, to_download.size());
      execute_downloads(fetch_specs, to_download, identity, r->tui_section);

      auto const duration{ std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count() };
      ENVY_TRACE_EXECUTE_DOWNLOADS_COMPLETE(identity, tid, to_download.size(), duration);

      bool const has_git_repos =
          std::any_of(fetch_specs.begin(), fetch_specs.end(), [](auto const &spec) {
            return std::holds_alternative<fetch_request_git>(spec.request);
          });

      if (has_git_repos) {
        tui::debug("phase fetch: returned spec contains git repos, not cacheable");
        should_mark_complete = false;
      }
    }
  } else {
    throw std::runtime_error("Fetch function for " + identity +
                             " must return nil, string, or table (got " +
                             sol::type_name(lua, return_value.get_type()) + ")");
  }

  // tmp_dir cleanup handled by lock destructor
  return should_mark_complete;
}

// Extract source, sha256, and ref from a Lua table.
table_entry parse_table_entry(sol::table const &tbl, std::string const &context) {
  std::string url{ sol_util_get_required<std::string>(tbl, "source", context) };
  if (url.empty()) {
    throw std::runtime_error("Fetch table 'source' field cannot be empty in " + context);
  }

  table_entry entry;
  entry.url = std::move(url);

  if (auto sha{ sol_util_get_optional<std::string>(tbl, "sha256", context) }) {
    entry.sha256 = std::move(*sha);
  }

  if (auto ref{ sol_util_get_optional<std::string>(tbl, "ref", context) }) {
    entry.ref = std::move(*ref);
  }

  return entry;
}

// Create fetch_spec from URL, SHA256, and optional ref, checking for filename collisions.
fetch_spec create_fetch_spec(std::string url,
                             std::string sha256,
                             std::optional<std::string> ref,
                             std::filesystem::path const &fetch_dir,
                             std::filesystem::path const &stage_dir,
                             std::unordered_set<std::string> &basenames,
                             std::string const &context) {
  std::string basename{ uri_extract_filename(url) };
  if (basename.empty()) {
    throw std::runtime_error("Cannot extract filename from URL: " + url + " in " +
                             context);
  }

  if (basenames.contains(basename)) {
    throw std::runtime_error("Fetch filename collision: " + basename + " in " + context);
  }
  basenames.insert(basename);

  auto const info{ uri_classify(url) };

  // Git repos go directly to stage_dir (no extraction needed), everything else to
  // fetch_dir
  std::filesystem::path const dest{ info.scheme == uri_scheme::GIT
                                        ? stage_dir / basename
                                        : fetch_dir / basename };

  return { .request = url_to_fetch_request(url, dest, ref, context),
           .sha256 = std::move(sha256) };
}

// Parse the fetch field from Lua into a vector of fetch_specs.
std::vector<fetch_spec> parse_fetch_field(sol::object const &fetch_obj,
                                          std::filesystem::path const &fetch_dir,
                                          std::filesystem::path const &stage_dir,
                                          std::string const &key) {
  sol::type const fetch_type{ fetch_obj.get_type() };

  switch (fetch_type) {
    case sol::type::string: {
      std::string const url{ fetch_obj.as<std::string>() };
      std::string basename{ uri_extract_filename(url) };
      if (basename.empty()) {
        throw std::runtime_error("Cannot extract filename from URL: " + url + " in " +
                                 key);
      }
      std::filesystem::path dest{ fetch_dir / basename };

      return { { .request = url_to_fetch_request(url, dest, std::nullopt, key),
                 .sha256 = "" } };
    }

    case sol::type::table: {
      std::vector<fetch_spec> specs;
      std::unordered_set<std::string> basenames;

      sol::table tbl{ fetch_obj.as<sol::table>() };

      sol::object first_elem{ tbl[1] };
      sol::type first_elem_type{ first_elem.get_type() };

      auto process_table_entry{ [&](sol::table const &entry_tbl) {
        auto entry{ parse_table_entry(entry_tbl, key) };
        specs.push_back(create_fetch_spec(std::move(entry.url),
                                          std::move(entry.sha256),
                                          std::move(entry.ref),
                                          fetch_dir,
                                          stage_dir,
                                          basenames,
                                          key));
      } };

      if (first_elem_type == sol::type::none || first_elem_type == sol::type::lua_nil) {
        process_table_entry(tbl);
      } else if (first_elem_type == sol::type::string) {
        size_t const len{ tbl.size() };
        for (size_t i = 1; i <= len; ++i) {
          sol::object elem{ tbl[i] };
          if (!elem.is<std::string>()) {
            throw std::runtime_error("Array element " + std::to_string(i) +
                                     " must be string in " + key);
          }
          std::string url{ elem.as<std::string>() };

          specs.push_back(create_fetch_spec(std::move(url),
                                            "",
                                            std::nullopt,
                                            fetch_dir,
                                            stage_dir,
                                            basenames,
                                            key));
        }
      } else if (first_elem_type == sol::type::table) {
        size_t const len{ tbl.size() };
        for (size_t i = 1; i <= len; ++i) {
          sol::table elem = tbl[i];
          process_table_entry(elem);
        }
      } else {
        throw std::runtime_error("Invalid fetch array element type in " + key);
      }

      return specs;
    }

    default:
      throw std::runtime_error("Fetch field must be string, table, or function in " + key);
  }
}

// Check cache and determine which files need downloading.
std::vector<size_t> determine_downloads_needed(std::vector<fetch_spec> const &specs) {
  std::vector<size_t> to_download;

  for (size_t i = 0; i < specs.size(); ++i) {
    auto const &spec{ specs[i] };
    auto const &dest{ get_destination(spec.request) };

    if (!std::filesystem::exists(dest)) {  // File doesn't exist: download
      to_download.push_back(i);
      continue;
    }

    if (spec.sha256.empty()) {  // No SHA256: always re-download (no cache trust)
      tui::debug("phase fetch: no SHA256 for %s, re-downloading (no cache)",
                 dest.filename().string().c_str());
      std::filesystem::remove(dest);
      to_download.push_back(i);
      continue;
    }

    // File exists with SHA256 - verify cached version
    try {
      tui::debug("phase fetch: verifying cached file %s", dest.string().c_str());
      sha256_verify(spec.sha256, sha256(dest));
      tui::debug("phase fetch: cache hit for %s", dest.filename().string().c_str());
    } catch (std::exception const &e) {  // hash mismatch, delete and re-download
      tui::debug("phase fetch: cache mismatch for %s, deleting", dest.string().c_str());
      std::filesystem::remove(dest);
      to_download.push_back(i);
    }
  }

  return to_download;
}

std::string fetch_item_label(fetch_request const &req) {
  return get_destination(req).filename().string();
}

tui::section_frame make_transfer_frame(std::string const &item_label,
                                       fetch_transfer_progress const &prog,
                                       bool include_item_label) {
  if (!prog.total.has_value() || *prog.total == 0) {  // Unknown total size
    std::ostringstream oss;
    if (include_item_label) { oss << item_label << " "; }
    oss << util_format_bytes(prog.transferred);
    return { .label = item_label,
             .content = tui::progress_data{ .percent = 0.0, .status = oss.str() } };
  }

  double const percent{ (prog.transferred / static_cast<double>(*prog.total)) * 100.0 };
  std::ostringstream oss;
  if (include_item_label) { oss << item_label << " "; }
  oss << util_format_bytes(prog.transferred) << "/" << util_format_bytes(*prog.total);
  return { .label = item_label,
           .content = tui::progress_data{ .percent = percent, .status = oss.str() } };
}

struct git_progress_state {
  double last_percent{ 0.0 };
  std::uint32_t max_total_objects{ 0 };
  std::uint32_t last_received_objects{ 0 };
  std::uint64_t last_bytes{ 0 };
};

struct fetch_tui_state {
  std::mutex mutex;
  std::vector<tui::section_frame> children;
  std::vector<git_progress_state> git_states;  // one per child
  bool grouped{ false };
  std::string phase_label;
  std::string label;
};

void set_fetch_frame(fetch_tui_state &state,
                     tui::section_handle section,
                     std::size_t idx,
                     tui::section_frame child_frame) {
  std::lock_guard const lock{ state.mutex };

  if (state.grouped) {
    if (idx < state.children.size()) { state.children[idx] = std::move(child_frame); }
    tui::section_frame parent{ .label = state.label,
                               .content =
                                   tui::static_text_data{ .text = state.phase_label },
                               .children = state.children,
                               .phase_label = {} };
    tui::section_set_content(section, parent);
  } else {
    child_frame.label = state.label;
    tui::section_set_content(section, child_frame);
  }
}

void update_progress_for_transfer(tui::section_handle handle,
                                  fetch_tui_state &state,
                                  std::size_t slot,
                                  fetch_transfer_progress const &prog) {
  if (slot >= state.children.size()) { return; }
  auto child_frame{
    make_transfer_frame(state.children[slot].label, prog, !state.grouped)
  };
  set_fetch_frame(state, handle, slot, std::move(child_frame));
}

void update_progress_for_git(tui::section_handle handle,
                             std::string const &label,
                             fetch_git_progress const &prog,
                             git_progress_state &state,
                             std::size_t slot,
                             fetch_tui_state &tui_state) {
  std::uint32_t snapshot_total{ 0 };
  std::uint32_t snapshot_received{ 0 };
  std::uint64_t snapshot_bytes{ 0 };
  double snapshot_percent{ 0.0 };
  std::string child_label{ label };

  {
    std::lock_guard const lock{ tui_state.mutex };
    state.max_total_objects = std::max(state.max_total_objects, prog.total_objects);
    state.last_received_objects =
        std::max(state.last_received_objects, prog.received_objects);
    state.last_bytes = std::max(state.last_bytes, prog.received_bytes);

    if (state.max_total_objects > 0) {
      double const pct =
          (state.last_received_objects / static_cast<double>(state.max_total_objects)) *
          100.0;
      state.last_percent = std::min(100.0, std::max(pct, state.last_percent));
    }

    snapshot_total = state.max_total_objects;
    snapshot_received = state.last_received_objects;
    snapshot_bytes = state.last_bytes;
    snapshot_percent = state.last_percent;
    if (slot < tui_state.children.size()) { child_label = tui_state.children[slot].label; }
  }

  if (snapshot_total == 0) {
    static auto const epoch{ std::chrono::steady_clock::time_point{} };
    std::string const prefix{ tui_state.grouped ? "" : child_label + " " };
    tui::section_frame child_frame{
      .label = child_label,
      .content = tui::spinner_data{ .text = prefix + "starting...", .start_time = epoch }
    };
    set_fetch_frame(tui_state, handle, slot, std::move(child_frame));
    return;
  }

  std::ostringstream oss;
  if (!tui_state.grouped) { oss << child_label << " "; }
  oss << snapshot_received << "/" << snapshot_total << " objects";
  if (snapshot_bytes > 0) { oss << " " << util_format_bytes(snapshot_bytes); }

  tui::section_frame child_frame{
    .label = child_label,
    .content = tui::progress_data{ .percent = snapshot_percent, .status = oss.str() }
  };
  if (snapshot_received >= snapshot_total) {
    child_frame.content = tui::static_text_data{ .text = oss.str() };
  }

  set_fetch_frame(tui_state, handle, slot, std::move(child_frame));
}

// Execute downloads and verification for specs that need downloading.
void execute_downloads(std::vector<fetch_spec> const &specs,
                       std::vector<size_t> const &to_download_indices,
                       std::string const &key,
                       tui::section_handle section) {
  if (to_download_indices.empty()) {
    tui::debug("phase fetch: all files cached, no downloads needed");
    return;
  }

  tui::debug("phase fetch: downloading %zu file(s)", to_download_indices.size());

  std::string const label{ "[" + key + "]" };

  fetch_tui_state tui_state{};
  tui_state.grouped = to_download_indices.size() > 1;
  tui_state.phase_label = "fetch";
  tui_state.label = label;
  tui_state.children.resize(to_download_indices.size());
  tui_state.git_states.resize(to_download_indices.size());

  std::vector<fetch_request> requests;
  requests.reserve(to_download_indices.size());
  for (size_t req_slot{ 0 }; req_slot < to_download_indices.size(); ++req_slot) {
    auto idx = to_download_indices[req_slot];
    fetch_request req{ specs[idx].request };

    std::string const item_label{ fetch_item_label(req) };
    tui_state.children[req_slot] =
        tui::section_frame{ .label = item_label,
                            .content = tui::progress_data{ .percent = 0.0,
                                                           .status = item_label } };

    std::visit(  // Set up progress callback
        [&](auto &r) {
          auto const child_index{ req_slot };
          r.progress = [section, label, &tui_state, child_index](
                           fetch_progress_t const &progress) -> bool {
            std::visit(
                match{
                    [&](fetch_transfer_progress const &prog) {
                      update_progress_for_transfer(section, tui_state, child_index, prog);
                    },
                    [&](fetch_git_progress const &prog) {
                      update_progress_for_git(section,
                                              label,
                                              prog,
                                              tui_state.git_states[child_index],
                                              child_index,
                                              tui_state);
                    },
                },
                progress);
            return true;  // Continue transfer
          };
        },
        req);

    requests.push_back(std::move(req));
  }

  // TODO: Add FETCH_FILE_START/COMPLETE trace events (requires passing recipe identity)

  auto const results{ fetch(requests) };

  std::vector<std::string> errors;
  for (size_t i = 0; i < results.size(); ++i) {
    auto const spec_idx{ to_download_indices[i] };
    std::string url{ get_source(specs[spec_idx].request) };

    if (auto const *err{ std::get_if<std::string>(&results[i]) }) {
      errors.push_back(url + ": " + *err);
    } else {
      // File downloaded successfully
      auto const *result{ std::get_if<fetch_result>(&results[i]) };
      if (result) {
        tui::debug("phase fetch: downloaded %s",
                   result->resolved_destination.filename().string().c_str());
      }

#ifdef ENVY_FUNCTIONAL_TESTER
      try {
        test::decrement_fail_counter();
      } catch (std::exception const &e) {
        errors.push_back(get_source(specs[spec_idx].request) + ": " + e.what());
        continue;
      }
#endif

      if (!specs[spec_idx].sha256.empty()) {
        try {
          auto const *result{ std::get_if<fetch_result>(&results[i]) };
          if (!result) { throw std::runtime_error("Unexpected result type"); }
          tui::debug("phase fetch: verifying SHA256 for %s",
                     result->resolved_destination.string().c_str());
          sha256_verify(specs[spec_idx].sha256, sha256(result->resolved_destination));
        } catch (std::exception const &e) {
          errors.push_back(get_source(specs[spec_idx].request) + ": " + e.what());
        }
      }
    }
  }

  if (!errors.empty()) {
    std::ostringstream oss;
    oss << "Fetch failed for " << key << ":\n";
    for (auto const &err : errors) { oss << "  " << err << "\n"; }
    throw std::runtime_error(oss.str());
  }
}

// fetch = "source" or fetch = {source="..."} or fetch = {{...}}
// Returns true if fetch should be marked complete (cacheable), false otherwise
bool run_declarative_fetch(sol::object const &fetch_obj,
                           cache::scoped_entry_lock *lock,
                           std::string const &identity,
                           recipe *r) {
  tui::debug("phase fetch: executing declarative fetch");

  // Ensure stage_dir exists (needed for git repos that clone directly there)
  std::error_code ec;
  std::filesystem::create_directories(lock->stage_dir(), ec);
  if (ec) {
    throw std::runtime_error("Failed to create stage directory: " + ec.message());
  }

  auto const fetch_specs{
    parse_fetch_field(fetch_obj, lock->fetch_dir(), lock->stage_dir(), identity)
  };
  if (fetch_specs.empty()) { return true; }  // No specs = cacheable (nothing to do)

  auto const tid{ std::hash<std::thread::id>{}(std::this_thread::get_id()) };
  tui::debug("[%s] starting execute_downloads (thread %zu)", identity.c_str(), tid);
  execute_downloads(fetch_specs,
                    determine_downloads_needed(fetch_specs),
                    identity,
                    r->tui_section);
  tui::debug("[%s] finished execute_downloads (thread %zu)", identity.c_str(), tid);

  // Check if git repos - if so, don't mark fetch complete (git clones are not cacheable)
  bool const has_git_repos{ std::any_of(fetch_specs.begin(),
                                        fetch_specs.end(),
                                        [](auto const &spec) {
                                          return std::holds_alternative<fetch_request_git>(
                                              spec.request);
                                        }) };

  if (has_git_repos) {
    tui::debug(
        "phase fetch: skipping fetch completion marker (git repos are not cacheable)");
    return false;
  }

  return true;  // Mark complete
}

}  // namespace

void run_fetch_phase(recipe *r, engine &eng) {
  phase_trace_scope const phase_scope{ r->spec->identity,
                                       recipe_phase::asset_fetch,
                                       std::chrono::steady_clock::now() };

  cache::scoped_entry_lock *lock = r->lock.get();
  if (!lock) {
    tui::debug("phase fetch: no lock (cache hit), skipping");
    return;
  }

  if (lock->is_fetch_complete()) {
    tui::debug("phase fetch: fetch already complete, skipping");
    return;
  }

  std::string const &identity{ r->spec->identity };

  sol::state_view lua_view{ *r->lua };
  sol::object fetch_obj{ lua_view["FETCH"] };

  bool should_mark_complete{ true };

  if (!fetch_obj.valid()) {
    tui::debug("phase fetch: no fetch field, skipping");
    return;
  } else if (fetch_obj.is<sol::protected_function>()) {
    should_mark_complete = run_programmatic_fetch(fetch_obj.as<sol::protected_function>(),
                                                  lock,
                                                  identity,
                                                  eng,
                                                  r);
  } else if (fetch_obj.is<std::string>() || fetch_obj.is<sol::table>()) {
    should_mark_complete = run_declarative_fetch(fetch_obj, lock, identity, r);
  } else {
    throw std::runtime_error("Fetch field must be nil, string, table, or function in " +
                             identity);
  }

  if (should_mark_complete) {
    lock->mark_fetch_complete();
    tui::debug("phase fetch: marked fetch complete");
  }
}

}  // namespace envy
