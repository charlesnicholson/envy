#include "tui.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

TEST_CASE("tui init can only run once") {
  CHECK_THROWS_AS(envy::tui::init(), std::logic_error);
}

TEST_CASE("tui allows handler changes while idle") {
  CHECK_NOTHROW(envy::tui::set_output_handler([](std::string_view) {}));
  CHECK_NOTHROW(envy::tui::set_output_handler([](std::string_view) {}));
}

TEST_CASE("tui enforces run/shutdown sequencing") {
  auto const handler{ [](std::string_view) {} };
  CHECK_NOTHROW(envy::tui::set_output_handler(handler));
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_INFO));
  CHECK_NOTHROW(envy::tui::shutdown());

  CHECK_NOTHROW(envy::tui::run(std::nullopt));
  CHECK_THROWS_AS(envy::tui::set_output_handler(handler), std::logic_error);
  CHECK_THROWS_AS(envy::tui::run(std::nullopt), std::logic_error);

  CHECK_NOTHROW(envy::tui::shutdown());
  CHECK_THROWS_AS(envy::tui::shutdown(), std::logic_error);

  CHECK_NOTHROW(envy::tui::set_output_handler(handler));
}

namespace {

struct captured_output {
  std::vector<std::string> messages;

  captured_output() {
    envy::tui::set_output_handler(
        [this](std::string_view value) { messages.emplace_back(value); });
  }

  ~captured_output() {
    try {
      envy::tui::set_output_handler([](std::string_view) {});
    } catch (std::logic_error const &error) {
      FAIL("set_output_handler should not throw during teardown: " << error.what());
    }
  }
};

std::string phase_token(char const *key, envy::recipe_phase phase) {
  std::string token{ "\"" };
  token.append(key);
  token.append("\":\"");
  token.append(envy::recipe_phase_name(phase));
  token.push_back('"');
  return token;
}

std::string phase_num_token(char const *key, envy::recipe_phase phase) {
  std::string token{ "\"" };
  token.append(key);
  token.append("_num\":");
  token.append(std::to_string(static_cast<int>(phase)));
  return token;
}

void expect_json_tokens(envy::trace_event_t const &event,
                        std::vector<std::string> tokens) {
  auto const json{ envy::trace_event_to_json(event) };
  CHECK_MESSAGE(json.find("\"ts\"") != std::string::npos, "missing timestamp in json");
  tokens.emplace_back(std::string{ "\"event\":\"" } +
                      std::string(envy::trace_event_name(event)) + "\"");
  for (auto const &token : tokens) {
    CHECK_MESSAGE(json.find(token) != std::string::npos,
                  "missing token: " << token << " in json: " << json);
  }
}

}  // namespace

TEST_CASE_FIXTURE(captured_output, "tui unstructured logs are raw messages") {
  REQUIRE(messages.empty());

  CHECK_NOTHROW(envy::tui::run(std::nullopt));

  envy::tui::debug("hello %s", "world");
  envy::tui::info("value %d", 42);
  envy::tui::warn("three %d", 3);
  envy::tui::error("boom");

  CHECK_NOTHROW(envy::tui::shutdown());

  REQUIRE(messages.size() == 4);
  CHECK(messages[0] == "hello world\n");
  CHECK(messages[1] == "value 42\n");
  CHECK(messages[2] == "three 3\n");
  CHECK(messages[3] == "boom\n");
}

TEST_CASE_FIXTURE(captured_output, "tui structured logs include prefix") {
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_DEBUG, true));
  envy::tui::info("structured %d", 7);
  CHECK_NOTHROW(envy::tui::shutdown());

  REQUIRE(messages.size() == 1);
  auto const &line{ messages[0] };
  CHECK(line.find("[INF") != std::string::npos);
  CHECK(line.rfind("structured 7\n") == line.size() - std::string("structured 7\n").size());
}

TEST_CASE_FIXTURE(captured_output, "tui severity filtering honors threshold") {
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_WARN, true));
  envy::tui::debug("debug");
  envy::tui::info("info");
  envy::tui::warn("warn");
  envy::tui::error("error");
  CHECK_NOTHROW(envy::tui::shutdown());

  REQUIRE(messages.size() == 2);
  CHECK(messages[0].find("WRN") != std::string::npos);
  CHECK(messages[0].find("warn") != std::string::npos);
  CHECK(messages[1].find("ERR") != std::string::npos);
  CHECK(messages[1].find("error") != std::string::npos);

  messages.clear();
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_INFO, true));
  envy::tui::debug("debug");
  envy::tui::info("info");
  CHECK_NOTHROW(envy::tui::shutdown());
  REQUIRE(messages.size() == 1);
  CHECK(messages[0].find("INF") != std::string::npos);
  CHECK(messages[0].find("info") != std::string::npos);
}

TEST_CASE_FIXTURE(captured_output, "tui trace events reach handler") {
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::stderr, std::nullopt } });
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));

  envy::tui::trace(envy::trace_events::phase_start{
      .recipe = "demo.recipe@v1",
      .phase = envy::recipe_phase::recipe_fetch,
  });

  CHECK_NOTHROW(envy::tui::shutdown());
  REQUIRE_FALSE(messages.empty());
  CHECK(messages[0].find("phase_start") != std::string::npos);
  CHECK(messages[0].find("recipe=demo.recipe@v1") != std::string::npos);

  envy::tui::configure_trace_outputs({});
}

TEST_CASE("trace_event_to_json serializes all event types") {
  expect_json_tokens(envy::trace_events::phase_blocked{
                         .recipe = "r1",
                         .blocked_at_phase = envy::recipe_phase::asset_check,
                         .waiting_for = "dep",
                         .target_phase = envy::recipe_phase::completion,
                     },
                     { "\"recipe\":\"r1\"",
                       phase_token("blocked_at_phase", envy::recipe_phase::asset_check),
                       phase_num_token("blocked_at_phase",
                                       envy::recipe_phase::asset_check),
                       "\"waiting_for\":\"dep\"",
                       phase_token("target_phase", envy::recipe_phase::completion),
                       phase_num_token("target_phase", envy::recipe_phase::completion) });

  expect_json_tokens(envy::trace_events::phase_unblocked{
                         .recipe = "r1",
                         .unblocked_at_phase = envy::recipe_phase::asset_check,
                         .dependency = "dep",
                     },
                     { "\"recipe\":\"r1\"",
                       phase_token("unblocked_at_phase", envy::recipe_phase::asset_check),
                       phase_num_token("unblocked_at_phase",
                                       envy::recipe_phase::asset_check),
                       "\"dependency\":\"dep\"" });

  expect_json_tokens(envy::trace_events::dependency_added{
                         .parent = "parent",
                         .dependency = "child",
                         .needed_by = envy::recipe_phase::asset_fetch,
                     },
                     { "\"parent\":\"parent\"",
                       "\"dependency\":\"child\"",
                       phase_token("needed_by", envy::recipe_phase::asset_fetch),
                       phase_num_token("needed_by", envy::recipe_phase::asset_fetch) });

  expect_json_tokens(envy::trace_events::phase_start{
                         .recipe = "r2",
                         .phase = envy::recipe_phase::asset_stage,
                     },
                     { "\"recipe\":\"r2\"",
                       phase_token("phase", envy::recipe_phase::asset_stage),
                       phase_num_token("phase", envy::recipe_phase::asset_stage) });

  expect_json_tokens(envy::trace_events::phase_complete{
                         .recipe = "r2",
                         .phase = envy::recipe_phase::asset_stage,
                         .duration_ms = 55,
                     },
                     { "\"recipe\":\"r2\"",
                       phase_token("phase", envy::recipe_phase::asset_stage),
                       phase_num_token("phase", envy::recipe_phase::asset_stage),
                       "\"duration_ms\":55" });

  expect_json_tokens(envy::trace_events::thread_start{
                         .recipe = "r3",
                         .target_phase = envy::recipe_phase::completion,
                     },
                     { "\"recipe\":\"r3\"",
                       phase_token("target_phase", envy::recipe_phase::completion),
                       phase_num_token("target_phase", envy::recipe_phase::completion) });

  expect_json_tokens(envy::trace_events::thread_complete{
                         .recipe = "r3",
                         .final_phase = envy::recipe_phase::asset_install,
                     },
                     { "\"recipe\":\"r3\"",
                       phase_token("final_phase", envy::recipe_phase::asset_install),
                       phase_num_token("final_phase", envy::recipe_phase::asset_install) });

  expect_json_tokens(envy::trace_events::recipe_registered{
                         .recipe = "r4",
                         .key = "k1",
                         .has_dependencies = true,
                     },
                     { "\"recipe\":\"r4\"",
                       "\"key\":\"k1\"",
                       "\"has_dependencies\":true" });

  expect_json_tokens(envy::trace_events::target_extended{
                         .recipe = "r4",
                         .old_target = envy::recipe_phase::asset_fetch,
                         .new_target = envy::recipe_phase::completion,
                     },
                     { "\"recipe\":\"r4\"",
                       phase_token("old_target", envy::recipe_phase::asset_fetch),
                       phase_num_token("old_target", envy::recipe_phase::asset_fetch),
                       phase_token("new_target", envy::recipe_phase::completion),
                       phase_num_token("new_target", envy::recipe_phase::completion) });

  expect_json_tokens(envy::trace_events::lua_ctx_run_start{
                         .recipe = "r5",
                         .command = "echo \"hi\"\n",
                         .cwd = "/tmp",
                     },
                     { "\"recipe\":\"r5\"",
                       "\"command\":\"echo \\\"hi\\\"\\n\"",
                       "\"cwd\":\"/tmp\"" });

  expect_json_tokens(envy::trace_events::lua_ctx_run_complete{
                         .recipe = "r5",
                         .exit_code = 7,
                         .duration_ms = 10,
                     },
                     { "\"recipe\":\"r5\"",
                       "\"exit_code\":7",
                       "\"duration_ms\":10" });

  expect_json_tokens(envy::trace_events::lua_ctx_fetch_start{
                         .recipe = "r6",
                         .url = "https://example.com",
                         .destination = "/cache/r6/file",
                     },
                     { "\"recipe\":\"r6\"",
                       "\"url\":\"https://example.com\"",
                       "\"destination\":\"/cache/r6/file\"" });

  expect_json_tokens(envy::trace_events::lua_ctx_fetch_complete{
                         .recipe = "r6",
                         .url = "https://example.com",
                         .bytes_downloaded = 1234,
                         .duration_ms = 42,
                     },
                     { "\"recipe\":\"r6\"",
                       "\"url\":\"https://example.com\"",
                       "\"bytes_downloaded\":1234",
                       "\"duration_ms\":42" });

  expect_json_tokens(envy::trace_events::lua_ctx_extract_start{
                         .recipe = "r7",
                         .archive_path = "/tmp/archive.tgz",
                         .destination = "/tmp/out",
                     },
                     { "\"recipe\":\"r7\"",
                       "\"archive_path\":\"/tmp/archive.tgz\"",
                       "\"destination\":\"/tmp/out\"" });

  expect_json_tokens(envy::trace_events::lua_ctx_extract_complete{
                         .recipe = "r7",
                         .files_extracted = 99,
                         .duration_ms = 5,
                     },
                     { "\"recipe\":\"r7\"",
                       "\"files_extracted\":99",
                       "\"duration_ms\":5" });

  expect_json_tokens(envy::trace_events::cache_hit{
                         .recipe = "r8",
                         .cache_key = "ck",
                         .asset_path = "/tmp/a",
                     },
                     { "\"recipe\":\"r8\"",
                       "\"cache_key\":\"ck\"",
                       "\"asset_path\":\"/tmp/a\"" });

  expect_json_tokens(envy::trace_events::cache_miss{
                         .recipe = "r8",
                         .cache_key = "ck",
                     },
                     { "\"recipe\":\"r8\"", "\"cache_key\":\"ck\"" });

  expect_json_tokens(envy::trace_events::lock_acquired{
                         .recipe = "r9",
                         .lock_path = "/tmp/l",
                         .wait_duration_ms = 3,
                     },
                     { "\"recipe\":\"r9\"",
                       "\"lock_path\":\"/tmp/l\"",
                       "\"wait_duration_ms\":3" });

  expect_json_tokens(envy::trace_events::lock_released{
                         .recipe = "r9",
                         .lock_path = "/tmp/l",
                         .hold_duration_ms = 15,
                     },
                     { "\"recipe\":\"r9\"",
                       "\"lock_path\":\"/tmp/l\"",
                       "\"hold_duration_ms\":15" });

  expect_json_tokens(envy::trace_events::fetch_file_start{
                         .recipe = "r10",
                         .url = "https://example.com/file",
                         .destination = "/tmp/dst",
                     },
                     { "\"recipe\":\"r10\"",
                       "\"url\":\"https://example.com/file\"",
                       "\"destination\":\"/tmp/dst\"" });

  expect_json_tokens(envy::trace_events::fetch_file_complete{
                         .recipe = "r10",
                         .url = "https://example.com/file",
                         .bytes_downloaded = 321,
                         .duration_ms = 8,
                         .from_cache = false,
                     },
                     { "\"recipe\":\"r10\"",
                       "\"url\":\"https://example.com/file\"",
                       "\"bytes_downloaded\":321",
                       "\"duration_ms\":8",
                       "\"from_cache\":false" });
}

TEST_CASE("trace_event_to_json escapes special characters") {
  // Test backslash escaping
  auto json{ envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\\back",
      .cache_key = "key",
      .asset_path = "path",
  }) };
  CHECK(json.find("r\\\\back") != std::string::npos);

  // Test quote escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\"quote",
      .cache_key = "key",
      .asset_path = "path",
  });
  CHECK(json.find("r\\\"quote") != std::string::npos);

  // Test newline escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\nline",
      .cache_key = "key",
      .asset_path = "path",
  });
  CHECK(json.find("r\\nline") != std::string::npos);

  // Test tab escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\ttab",
      .cache_key = "key",
      .asset_path = "path",
  });
  CHECK(json.find("r\\ttab") != std::string::npos);

  // Test carriage return escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\rreturn",
      .cache_key = "key",
      .asset_path = "path",
  });
  CHECK(json.find("r\\rreturn") != std::string::npos);

  // Test form feed escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\fform",
      .cache_key = "key",
      .asset_path = "path",
  });
  CHECK(json.find("r\\fform") != std::string::npos);

  // Test backspace escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = "r\bback",
      .cache_key = "key",
      .asset_path = "path",
  });
  CHECK(json.find("r\\bback") != std::string::npos);

  // Test control character escaping (control character \x01 represented as \u0001)
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .recipe = std::string("r\x01" "ctrl", 6),
      .cache_key = "key",
      .asset_path = "path",
  });
  // Check for the hex escape sequence (lowercase hex digits from %04x format)
  CHECK((json.find("\\u0001") != std::string::npos ||
         json.find("r\\u0001ctrl") != std::string::npos));
}

TEST_CASE("trace_event_to_json produces valid ISO8601 timestamps") {
  auto const json{ envy::trace_event_to_json(envy::trace_events::phase_start{
      .recipe = "test",
      .phase = envy::recipe_phase::recipe_fetch,
  }) };

  // Check for ISO8601 format: YYYY-MM-DDTHH:MM:SS.sssZ
  auto const ts_start{ json.find("\"ts\":\"") };
  REQUIRE(ts_start != std::string::npos);

  auto const ts_value_start{ ts_start + 6 };
  auto const ts_end{ json.find("\"", ts_value_start) };
  REQUIRE(ts_end != std::string::npos);

  auto const timestamp{ json.substr(ts_value_start, ts_end - ts_value_start) };

  // Verify format: YYYY-MM-DDTHH:MM:SS.sssZ (length 24)
  CHECK(timestamp.size() == 24);
  CHECK(timestamp[4] == '-');
  CHECK(timestamp[7] == '-');
  CHECK(timestamp[10] == 'T');
  CHECK(timestamp[13] == ':');
  CHECK(timestamp[16] == ':');
  CHECK(timestamp[19] == '.');
  CHECK(timestamp[23] == 'Z');

  // Verify all expected positions are digits
  for (int i : { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21, 22 }) {
    CHECK_MESSAGE(std::isdigit(timestamp[i]),
                  "Position " << i << " should be digit, got: " << timestamp[i]);
  }
}

TEST_CASE("g_trace_enabled controls trace event processing") {
  // Initially disabled (no trace outputs configured)
  CHECK_FALSE(envy::tui::g_trace_enabled);

  // Enable stderr trace
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::stderr, std::nullopt } });
  CHECK(envy::tui::g_trace_enabled);

  // Disable trace
  envy::tui::configure_trace_outputs({});
  CHECK_FALSE(envy::tui::g_trace_enabled);

  // Enable file trace
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::file,
          std::filesystem::temp_directory_path() / "test_trace.jsonl" } });
  CHECK(envy::tui::g_trace_enabled);

  // Disable again
  envy::tui::configure_trace_outputs({});
  CHECK_FALSE(envy::tui::g_trace_enabled);

  // Enable both
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::stderr, std::nullopt },
        { envy::tui::trace_output_type::file,
          std::filesystem::temp_directory_path() / "test_trace2.jsonl" } });
  CHECK(envy::tui::g_trace_enabled);

  // Cleanup
  envy::tui::configure_trace_outputs({});
}

TEST_CASE("trace_event_to_string formats human-readable output") {
  // Test phase_blocked - phase names are shortened (build not asset_build)
  auto output{ envy::trace_event_to_string(envy::trace_events::phase_blocked{
      .recipe = "parent@v1",
      .blocked_at_phase = envy::recipe_phase::asset_build,
      .waiting_for = "dep@v2",
      .target_phase = envy::recipe_phase::completion,
  }) };
  CHECK(output.find("phase_blocked") != std::string::npos);
  CHECK(output.find("recipe=parent@v1") != std::string::npos);
  CHECK(output.find("blocked_at=build") != std::string::npos);
  CHECK(output.find("waiting_for=dep@v2") != std::string::npos);
  CHECK(output.find("target_phase=completion") != std::string::npos);

  // Test dependency_added - phase name is "fetch" not "asset_fetch"
  output = envy::trace_event_to_string(envy::trace_events::dependency_added{
      .parent = "p@v1",
      .dependency = "d@v2",
      .needed_by = envy::recipe_phase::asset_fetch,
  });
  CHECK(output.find("dependency_added") != std::string::npos);
  CHECK(output.find("parent=p@v1") != std::string::npos);
  CHECK(output.find("dependency=d@v2") != std::string::npos);
  CHECK(output.find("needed_by=fetch") != std::string::npos);

  // Test cache_hit
  output = envy::trace_event_to_string(envy::trace_events::cache_hit{
      .recipe = "r@v1",
      .cache_key = "key123",
      .asset_path = "/cache/path",
  });
  CHECK(output.find("cache_hit") != std::string::npos);
  CHECK(output.find("recipe=r@v1") != std::string::npos);
  CHECK(output.find("cache_key=key123") != std::string::npos);
  CHECK(output.find("asset_path=/cache/path") != std::string::npos);

  // Test lock_acquired
  output = envy::trace_event_to_string(envy::trace_events::lock_acquired{
      .recipe = "r@v1",
      .lock_path = "/locks/entry",
      .wait_duration_ms = 150,
  });
  CHECK(output.find("lock_acquired") != std::string::npos);
  CHECK(output.find("recipe=r@v1") != std::string::npos);
  CHECK(output.find("lock_path=/locks/entry") != std::string::npos);
  CHECK(output.find("wait_ms=150") != std::string::npos);
}

TEST_CASE("trace event macros work with g_trace_enabled") {
  envy::tui::configure_trace_outputs({});
  CHECK_FALSE(envy::tui::g_trace_enabled);

  // These should not crash even when trace disabled
  ENVY_TRACE_PHASE_BLOCKED("r1", envy::recipe_phase::asset_check, "dep", envy::recipe_phase::completion);
  ENVY_TRACE_DEPENDENCY_ADDED("parent", "child", envy::recipe_phase::asset_fetch);
  ENVY_TRACE_CACHE_HIT("r1", "key", "/path");

  // Enable trace and verify events can be emitted
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::stderr, std::nullopt } });
  CHECK(envy::tui::g_trace_enabled);

  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));
  ENVY_TRACE_PHASE_START("test", envy::recipe_phase::recipe_fetch);
  CHECK_NOTHROW(envy::tui::shutdown());

  envy::tui::configure_trace_outputs({});
}

TEST_CASE("trace file output writes JSONL format") {
  auto const trace_path{ std::filesystem::temp_directory_path() / "envy_test_trace.jsonl" };

  // Clean up any existing file
  std::error_code ec;
  std::filesystem::remove(trace_path, ec);

  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::file, trace_path } });
  CHECK(envy::tui::g_trace_enabled);

  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));

  // Emit various trace events
  envy::tui::trace(envy::trace_events::phase_start{
      .recipe = "test@v1",
      .phase = envy::recipe_phase::recipe_fetch,
  });

  envy::tui::trace(envy::trace_events::dependency_added{
      .parent = "parent@v1",
      .dependency = "child@v2",
      .needed_by = envy::recipe_phase::asset_fetch,
  });

  envy::tui::trace(envy::trace_events::cache_hit{
      .recipe = "test@v1",
      .cache_key = "test-key",
      .asset_path = "/cache/test",
  });

  CHECK_NOTHROW(envy::tui::shutdown());

  // Verify file exists and contains JSON
  REQUIRE(std::filesystem::exists(trace_path));

  std::ifstream file{ trace_path };
  REQUIRE(file.is_open());

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty()) { lines.push_back(line); }
  }
  file.close();

  // Should have 3 events
  CHECK(lines.size() >= 3);

  // Each line should be valid JSON with expected fields
  for (auto const &json_line : lines) {
    CHECK(json_line.find("\"ts\":") != std::string::npos);
    CHECK(json_line.find("\"event\":") != std::string::npos);
    CHECK(json_line[0] == '{');
    CHECK(json_line[json_line.size() - 1] == '}');
  }

  // Verify specific events
  bool found_phase_start{ false };
  bool found_dependency_added{ false };
  bool found_cache_hit{ false };

  for (auto const &json_line : lines) {
    if (json_line.find("\"event\":\"phase_start\"") != std::string::npos &&
        json_line.find("\"recipe\":\"test@v1\"") != std::string::npos) {
      found_phase_start = true;
    }
    if (json_line.find("\"event\":\"dependency_added\"") != std::string::npos &&
        json_line.find("\"parent\":\"parent@v1\"") != std::string::npos) {
      found_dependency_added = true;
    }
    if (json_line.find("\"event\":\"cache_hit\"") != std::string::npos &&
        json_line.find("\"cache_key\":\"test-key\"") != std::string::npos) {
      found_cache_hit = true;
    }
  }

  CHECK(found_phase_start);
  CHECK(found_dependency_added);
  CHECK(found_cache_hit);

  // Cleanup
  std::filesystem::remove(trace_path, ec);
  envy::tui::configure_trace_outputs({});
}

TEST_CASE_FIXTURE(captured_output, "trace multiple outputs simultaneously") {
  auto const trace_path{ std::filesystem::temp_directory_path() /
                         "envy_test_multi_trace.jsonl" };

  // Clean up any existing file
  std::error_code ec;
  std::filesystem::remove(trace_path, ec);

  // Configure both stderr and file output
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::stderr, std::nullopt },
        { envy::tui::trace_output_type::file, trace_path } });
  CHECK(envy::tui::g_trace_enabled);

  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));

  // Emit trace event
  envy::tui::trace(envy::trace_events::phase_complete{
      .recipe = "multi@v1",
      .phase = envy::recipe_phase::asset_build,
      .duration_ms = 123,
  });

  CHECK_NOTHROW(envy::tui::shutdown());

  // Verify stderr output (human-readable)
  REQUIRE_FALSE(messages.empty());
  bool found_stderr{ false };
  for (auto const &msg : messages) {
    if (msg.find("phase_complete") != std::string::npos &&
        msg.find("recipe=multi@v1") != std::string::npos) {
      found_stderr = true;
      break;
    }
  }
  CHECK(found_stderr);

  // Verify file output (JSON)
  REQUIRE(std::filesystem::exists(trace_path));

  std::ifstream file{ trace_path };
  REQUIRE(file.is_open());

  bool found_file{ false };
  std::string line;
  while (std::getline(file, line)) {
    if (line.find("\"event\":\"phase_complete\"") != std::string::npos &&
        line.find("\"recipe\":\"multi@v1\"") != std::string::npos &&
        line.find("\"duration_ms\":123") != std::string::npos) {
      found_file = true;
      break;
    }
  }
  file.close();

  CHECK(found_file);

  // Cleanup
  std::filesystem::remove(trace_path, ec);
  envy::tui::configure_trace_outputs({});
}

TEST_CASE("configure_trace_outputs rejects multiple file outputs") {
  auto const path1{ std::filesystem::temp_directory_path() / "trace1.jsonl" };
  auto const path2{ std::filesystem::temp_directory_path() / "trace2.jsonl" };

  CHECK_THROWS_AS(envy::tui::configure_trace_outputs(
                      { { envy::tui::trace_output_type::file, path1 },
                        { envy::tui::trace_output_type::file, path2 } }),
                  std::logic_error);

  // Ensure cleanup in case of test failure
  envy::tui::configure_trace_outputs({});
}
