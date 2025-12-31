#include "tui.h"

#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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

std::string phase_token(char const *key, envy::pkg_phase phase) {
  std::string token{ "\"" };
  token.append(key);
  token.append("\":\"");
  token.append(envy::pkg_phase_name(phase));
  token.push_back('"');
  return token;
}

std::string phase_num_token(char const *key, envy::pkg_phase phase) {
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
  CHECK(line.rfind("structured 7\n") ==
        line.size() - std::string("structured 7\n").size());
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
      { { envy::tui::trace_output_type::std_err, std::nullopt } });
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));

  envy::tui::trace(envy::trace_events::phase_start{
      .spec = "demo.spec@v1",
      .phase = envy::pkg_phase::spec_fetch,
  });

  CHECK_NOTHROW(envy::tui::shutdown());
  REQUIRE_FALSE(messages.empty());
  CHECK(messages[0].find("phase_start") != std::string::npos);
  CHECK(messages[0].find("spec=demo.spec@v1") != std::string::npos);

  envy::tui::configure_trace_outputs({});
}

TEST_CASE("trace_event_to_json serializes all event types") {
  expect_json_tokens(
      envy::trace_events::phase_blocked{
          .spec = "r1",
          .blocked_at_phase = envy::pkg_phase::pkg_check,
          .waiting_for = "dep",
          .target_phase = envy::pkg_phase::completion,
      },
      { "\"spec\":\"r1\"",
        phase_token("blocked_at_phase", envy::pkg_phase::pkg_check),
        phase_num_token("blocked_at_phase", envy::pkg_phase::pkg_check),
        "\"waiting_for\":\"dep\"",
        phase_token("target_phase", envy::pkg_phase::completion),
        phase_num_token("target_phase", envy::pkg_phase::completion) });

  expect_json_tokens(
      envy::trace_events::phase_unblocked{
          .spec = "r1",
          .unblocked_at_phase = envy::pkg_phase::pkg_check,
          .dependency = "dep",
      },
      { "\"spec\":\"r1\"",
        phase_token("unblocked_at_phase", envy::pkg_phase::pkg_check),
        phase_num_token("unblocked_at_phase", envy::pkg_phase::pkg_check),
        "\"dependency\":\"dep\"" });

  expect_json_tokens(
      envy::trace_events::dependency_added{
          .parent = "parent",
          .dependency = "child",
          .needed_by = envy::pkg_phase::pkg_fetch,
      },
      { "\"parent\":\"parent\"",
        "\"dependency\":\"child\"",
        phase_token("needed_by", envy::pkg_phase::pkg_fetch),
        phase_num_token("needed_by", envy::pkg_phase::pkg_fetch) });

  expect_json_tokens(
      envy::trace_events::phase_start{
          .spec = "r2",
          .phase = envy::pkg_phase::pkg_stage,
      },
      { "\"spec\":\"r2\"",
        phase_token("phase", envy::pkg_phase::pkg_stage),
        phase_num_token("phase", envy::pkg_phase::pkg_stage) });

  expect_json_tokens(
      envy::trace_events::phase_complete{
          .spec = "r2",
          .phase = envy::pkg_phase::pkg_stage,
          .duration_ms = 55,
      },
      { "\"spec\":\"r2\"",
        phase_token("phase", envy::pkg_phase::pkg_stage),
        phase_num_token("phase", envy::pkg_phase::pkg_stage),
        "\"duration_ms\":55" });

  expect_json_tokens(
      envy::trace_events::thread_start{
          .spec = "r3",
          .target_phase = envy::pkg_phase::completion,
      },
      { "\"spec\":\"r3\"",
        phase_token("target_phase", envy::pkg_phase::completion),
        phase_num_token("target_phase", envy::pkg_phase::completion) });

  expect_json_tokens(
      envy::trace_events::thread_complete{
          .spec = "r3",
          .final_phase = envy::pkg_phase::pkg_install,
      },
      { "\"spec\":\"r3\"",
        phase_token("final_phase", envy::pkg_phase::pkg_install),
        phase_num_token("final_phase", envy::pkg_phase::pkg_install) });

  expect_json_tokens(
      envy::trace_events::spec_registered{
          .spec = "r4",
          .key = "k1",
          .has_dependencies = true,
      },
      { "\"spec\":\"r4\"", "\"key\":\"k1\"", "\"has_dependencies\":true" });

  expect_json_tokens(
      envy::trace_events::target_extended{
          .spec = "r4",
          .old_target = envy::pkg_phase::pkg_fetch,
          .new_target = envy::pkg_phase::completion,
      },
      { "\"spec\":\"r4\"",
        phase_token("old_target", envy::pkg_phase::pkg_fetch),
        phase_num_token("old_target", envy::pkg_phase::pkg_fetch),
        phase_token("new_target", envy::pkg_phase::completion),
        phase_num_token("new_target", envy::pkg_phase::completion) });

  expect_json_tokens(
      envy::trace_events::lua_ctx_run_start{
          .spec = "r5",
          .command = "echo \"hi\"\n",
          .cwd = "/tmp",
      },
      { "\"spec\":\"r5\"", "\"command\":\"echo \\\"hi\\\"\\n\"", "\"cwd\":\"/tmp\"" });

  expect_json_tokens(
      envy::trace_events::lua_ctx_run_complete{
          .spec = "r5",
          .exit_code = 7,
          .duration_ms = 10,
      },
      { "\"spec\":\"r5\"", "\"exit_code\":7", "\"duration_ms\":10" });

  expect_json_tokens(
      envy::trace_events::lua_ctx_fetch_start{
          .spec = "r6",
          .url = "https://example.com",
          .destination = "/cache/r6/file",
      },
      { "\"spec\":\"r6\"",
        "\"url\":\"https://example.com\"",
        "\"destination\":\"/cache/r6/file\"" });

  expect_json_tokens(
      envy::trace_events::lua_ctx_fetch_complete{
          .spec = "r6",
          .url = "https://example.com",
          .bytes_downloaded = 1234,
          .duration_ms = 42,
      },
      { "\"spec\":\"r6\"",
        "\"url\":\"https://example.com\"",
        "\"bytes_downloaded\":1234",
        "\"duration_ms\":42" });

  expect_json_tokens(
      envy::trace_events::lua_ctx_extract_start{
          .spec = "r7",
          .archive_path = "/tmp/archive.tgz",
          .destination = "/tmp/out",
      },
      { "\"spec\":\"r7\"",
        "\"archive_path\":\"/tmp/archive.tgz\"",
        "\"destination\":\"/tmp/out\"" });

  expect_json_tokens(
      envy::trace_events::lua_ctx_extract_complete{
          .spec = "r7",
          .files_extracted = 99,
          .duration_ms = 5,
      },
      { "\"spec\":\"r7\"", "\"files_extracted\":99", "\"duration_ms\":5" });

  expect_json_tokens(
      envy::trace_events::cache_hit{
          .spec = "r8",
          .cache_key = "ck",
          .pkg_path = "/tmp/a",
      },
      { "\"spec\":\"r8\"", "\"cache_key\":\"ck\"", "\"pkg_path\":\"/tmp/a\"" });

  expect_json_tokens(
      envy::trace_events::cache_miss{
          .spec = "r8",
          .cache_key = "ck",
      },
      { "\"spec\":\"r8\"", "\"cache_key\":\"ck\"" });

  expect_json_tokens(
      envy::trace_events::lock_acquired{
          .spec = "r9",
          .lock_path = "/tmp/l",
          .wait_duration_ms = 3,
      },
      { "\"spec\":\"r9\"", "\"lock_path\":\"/tmp/l\"", "\"wait_duration_ms\":3" });

  expect_json_tokens(
      envy::trace_events::lock_released{
          .spec = "r9",
          .lock_path = "/tmp/l",
          .hold_duration_ms = 15,
      },
      { "\"spec\":\"r9\"", "\"lock_path\":\"/tmp/l\"", "\"hold_duration_ms\":15" });

  expect_json_tokens(
      envy::trace_events::fetch_file_start{
          .spec = "r10",
          .url = "https://example.com/file",
          .destination = "/tmp/dst",
      },
      { "\"spec\":\"r10\"",
        "\"url\":\"https://example.com/file\"",
        "\"destination\":\"/tmp/dst\"" });

  expect_json_tokens(
      envy::trace_events::fetch_file_complete{
          .spec = "r10",
          .url = "https://example.com/file",
          .bytes_downloaded = 321,
          .duration_ms = 8,
          .from_cache = false,
      },
      { "\"spec\":\"r10\"",
        "\"url\":\"https://example.com/file\"",
        "\"bytes_downloaded\":321",
        "\"duration_ms\":8",
        "\"from_cache\":false" });
}

TEST_CASE("trace_event_to_json escapes special characters") {
  // Test backslash escaping
  auto json{ envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\\back",
      .cache_key = "key",
      .pkg_path = "path",
  }) };
  CHECK(json.find("r\\\\back") != std::string::npos);

  // Test quote escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\"quote",
      .cache_key = "key",
      .pkg_path = "path",
  });
  CHECK(json.find("r\\\"quote") != std::string::npos);

  // Test newline escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\nline",
      .cache_key = "key",
      .pkg_path = "path",
  });
  CHECK(json.find("r\\nline") != std::string::npos);

  // Test tab escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\ttab",
      .cache_key = "key",
      .pkg_path = "path",
  });
  CHECK(json.find("r\\ttab") != std::string::npos);

  // Test carriage return escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\rreturn",
      .cache_key = "key",
      .pkg_path = "path",
  });
  CHECK(json.find("r\\rreturn") != std::string::npos);

  // Test form feed escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\fform",
      .cache_key = "key",
      .pkg_path = "path",
  });
  CHECK(json.find("r\\fform") != std::string::npos);

  // Test backspace escaping
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = "r\bback",
      .cache_key = "key",
      .pkg_path = "path",
  });
  CHECK(json.find("r\\bback") != std::string::npos);

  // Test control character escaping (control character \x01 represented as \u0001)
  json = envy::trace_event_to_json(envy::trace_events::cache_hit{
      .spec = std::string("r\x01"
                            "ctrl",
                            6),
      .cache_key = "key",
      .pkg_path = "path",
  });
  // Check for the hex escape sequence (lowercase hex digits from %04x format)
  CHECK((json.find("\\u0001") != std::string::npos ||
         json.find("r\\u0001ctrl") != std::string::npos));
}

TEST_CASE("trace_event_to_json produces valid ISO8601 timestamps") {
  auto const json{ envy::trace_event_to_json(envy::trace_events::phase_start{
      .spec = "test",
      .phase = envy::pkg_phase::spec_fetch,
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
      { { envy::tui::trace_output_type::std_err, std::nullopt } });
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
      { { envy::tui::trace_output_type::std_err, std::nullopt },
        { envy::tui::trace_output_type::file,
          std::filesystem::temp_directory_path() / "test_trace2.jsonl" } });
  CHECK(envy::tui::g_trace_enabled);

  // Cleanup
  envy::tui::configure_trace_outputs({});
}

TEST_CASE("trace_event_to_string formats human-readable output") {
  // Test phase_blocked - phase names are shortened (build not asset_build)
  auto output{ envy::trace_event_to_string(envy::trace_events::phase_blocked{
      .spec = "parent@v1",
      .blocked_at_phase = envy::pkg_phase::pkg_build,
      .waiting_for = "dep@v2",
      .target_phase = envy::pkg_phase::completion,
  }) };
  CHECK(output.find("phase_blocked") != std::string::npos);
  CHECK(output.find("spec=parent@v1") != std::string::npos);
  CHECK(output.find("blocked_at=build") != std::string::npos);
  CHECK(output.find("waiting_for=dep@v2") != std::string::npos);
  CHECK(output.find("target_phase=completion") != std::string::npos);

  // Test dependency_added - phase name is "fetch" not "asset_fetch"
  output = envy::trace_event_to_string(envy::trace_events::dependency_added{
      .parent = "p@v1",
      .dependency = "d@v2",
      .needed_by = envy::pkg_phase::pkg_fetch,
  });
  CHECK(output.find("dependency_added") != std::string::npos);
  CHECK(output.find("parent=p@v1") != std::string::npos);
  CHECK(output.find("dependency=d@v2") != std::string::npos);
  CHECK(output.find("needed_by=fetch") != std::string::npos);

  // Test cache_hit
  output = envy::trace_event_to_string(envy::trace_events::cache_hit{
      .spec = "r@v1",
      .cache_key = "key123",
      .pkg_path = "/cache/path",
  });
  CHECK(output.find("cache_hit") != std::string::npos);
  CHECK(output.find("spec=r@v1") != std::string::npos);
  CHECK(output.find("cache_key=key123") != std::string::npos);
  CHECK(output.find("pkg_path=/cache/path") != std::string::npos);

  // Test lock_acquired
  output = envy::trace_event_to_string(envy::trace_events::lock_acquired{
      .spec = "r@v1",
      .lock_path = "/locks/entry",
      .wait_duration_ms = 150,
  });
  CHECK(output.find("lock_acquired") != std::string::npos);
  CHECK(output.find("spec=r@v1") != std::string::npos);
  CHECK(output.find("lock_path=/locks/entry") != std::string::npos);
  CHECK(output.find("wait_ms=150") != std::string::npos);
}

TEST_CASE("trace event macros work with g_trace_enabled") {
  envy::tui::configure_trace_outputs({});
  CHECK_FALSE(envy::tui::g_trace_enabled);

  // These should not crash even when trace disabled
  ENVY_TRACE_PHASE_BLOCKED("r1",
                           envy::pkg_phase::pkg_check,
                           "dep",
                           envy::pkg_phase::completion);
  ENVY_TRACE_DEPENDENCY_ADDED("parent", "child", envy::pkg_phase::pkg_fetch);
  ENVY_TRACE_CACHE_HIT("r1", "key", "/path", true);

  // Enable trace and verify events can be emitted
  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::std_err, std::nullopt } });
  CHECK(envy::tui::g_trace_enabled);

  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));
  ENVY_TRACE_PHASE_START("test", envy::pkg_phase::spec_fetch);
  CHECK_NOTHROW(envy::tui::shutdown());

  envy::tui::configure_trace_outputs({});
}

TEST_CASE("trace file output writes JSONL format") {
  auto const trace_path{ std::filesystem::temp_directory_path() /
                         "envy_test_trace.jsonl" };

  // Clean up any existing file
  std::error_code ec;
  std::filesystem::remove(trace_path, ec);

  envy::tui::configure_trace_outputs(
      { { envy::tui::trace_output_type::file, trace_path } });
  CHECK(envy::tui::g_trace_enabled);

  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));

  // Emit various trace events
  envy::tui::trace(envy::trace_events::phase_start{
      .spec = "test@v1",
      .phase = envy::pkg_phase::spec_fetch,
  });

  envy::tui::trace(envy::trace_events::dependency_added{
      .parent = "parent@v1",
      .dependency = "child@v2",
      .needed_by = envy::pkg_phase::pkg_fetch,
  });

  envy::tui::trace(envy::trace_events::cache_hit{
      .spec = "test@v1",
      .cache_key = "test-key",
      .pkg_path = "/cache/test",
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
        json_line.find("\"spec\":\"test@v1\"") != std::string::npos) {
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
      { { envy::tui::trace_output_type::std_err, std::nullopt },
        { envy::tui::trace_output_type::file, trace_path } });
  CHECK(envy::tui::g_trace_enabled);

  CHECK_NOTHROW(envy::tui::run(envy::tui::level::TUI_TRACE, false));

  // Emit trace event
  envy::tui::trace(envy::trace_events::phase_complete{
      .spec = "multi@v1",
      .phase = envy::pkg_phase::pkg_build,
      .duration_ms = 123,
  });

  CHECK_NOTHROW(envy::tui::shutdown());

  // Verify stderr output (human-readable)
  REQUIRE_FALSE(messages.empty());
  bool found_stderr{ false };
  for (auto const &msg : messages) {
    if (msg.find("phase_complete") != std::string::npos &&
        msg.find("spec=multi@v1") != std::string::npos) {
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
        line.find("\"spec\":\"multi@v1\"") != std::string::npos &&
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

// Progress section tests

#ifdef ENVY_UNIT_TEST

TEST_CASE("progress section line counting with text stream") {
  envy::tui::test::g_terminal_width = 80;
  envy::tui::test::g_isatty = true;
  auto const now{ std::chrono::steady_clock::now() };
  envy::tui::test::g_now = now;

  envy::tui::section_frame const frame{ .label = "pkg@v1",
                                        .content = envy::tui::text_stream_data{
                                            .lines = { "line1", "line2", "line3" },
                                            .start_time = now } };

  std::string const output{ envy::tui::test::render_section_frame(frame) };

  // Should have: 1 header line + 3 content lines = 4 lines total
  int const line_count{ static_cast<int>(std::count(output.begin(), output.end(), '\n')) };
  CHECK(line_count == 4);
}

TEST_CASE("grouped render ansi") {
  envy::tui::test::g_terminal_width = 80;
  envy::tui::test::g_isatty = true;
  auto const now{ std::chrono::steady_clock::now() };
  envy::tui::test::g_now = now;

  envy::tui::section_frame parent{
    .label = "pkg",
    .content = envy::tui::progress_data{ .percent = 50.0, .status = "fetch" },
    .phase_label = ""
  };
  parent.children.push_back(envy::tui::section_frame{
      .label = "ninja.git",
      .content = envy::tui::progress_data{ .percent = 20.0, .status = "20%" } });
  parent.children.push_back(envy::tui::section_frame{
      .label = "googletest.git",
      .content = envy::tui::progress_data{ .percent = 80.0, .status = "80%" } });

  std::string const output{ envy::tui::test::render_section_frame(parent) };

  CHECK(output.find("pkg") != std::string::npos);
  CHECK(output.find("fetch") != std::string::npos);
  CHECK(output.find("  ninja.git") != std::string::npos);
  CHECK(output.find("  googletest.git") != std::string::npos);
}

TEST_CASE("grouped render fallback") {
  envy::tui::test::g_terminal_width = 80;
  envy::tui::test::g_isatty = false;
  auto const now{ std::chrono::steady_clock::now() };
  envy::tui::test::g_now = now;

  envy::tui::section_frame parent{
    .label = "pkg",
    .content = envy::tui::progress_data{ .percent = 50.0, .status = "fetch" },
    .phase_label = ""
  };
  parent.children.push_back(envy::tui::section_frame{
      .label = "ninja.git",
      .content = envy::tui::progress_data{ .percent = 20.0, .status = "20%" } });

  std::string const output{ envy::tui::test::render_section_frame(parent) };

  CHECK(output.find("pkg") != std::string::npos);
  CHECK(output.find("fetch") != std::string::npos);
  CHECK(output.find("  ninja.git") != std::string::npos);
}

TEST_CASE("inactive sections do not render") {
  auto const h1{ envy::tui::section_create() };
  auto const h2{ envy::tui::section_create() };

  envy::tui::section_frame const frame{ .label = "pkg@v1",
                                        .content = envy::tui::static_text_data{
                                            .text = "test" } };

  envy::tui::section_set_content(h1, frame);
  envy::tui::section_set_content(h2, frame);

  // Release h1 - it should not render
  envy::tui::section_release(h1);

  // Can't directly test render output without exposing internals,
  // but we can verify the section was marked inactive
  // (This is a structural test - render functions check active flag)
}

TEST_CASE("interactive_mode_guard RAII") {
  {
    envy::tui::interactive_mode_guard guard;
    // Mutex is locked, rendering paused
  }
  // Mutex is unlocked, rendering resumed (destructor ran)

  // If we can create another guard without deadlock, RAII worked
  { envy::tui::interactive_mode_guard guard2; }
}

TEST_CASE("interactive_mode_guard exception safety") {
  bool exception_thrown{ false };

  try {
    envy::tui::interactive_mode_guard guard;
    exception_thrown = true;
    throw std::runtime_error{ "test exception" };
  } catch (std::runtime_error const &) {}

  CHECK(exception_thrown);

  // If we can acquire the guard again, the mutex was properly released
  { envy::tui::interactive_mode_guard guard2; }
}

TEST_CASE("serialized interactive mode") {
  std::mutex sync_mutex;
  std::condition_variable sync_cv;
  std::atomic<int> counter{ 0 };
  bool t1_acquired{ false };
  bool t2_attempting{ false };

  std::thread t1{ [&]() {
    envy::tui::interactive_mode_guard guard;
    counter++;

    {  // Signal t2 that we've acquired
      std::lock_guard lock{ sync_mutex };
      t1_acquired = true;
    }
    sync_cv.notify_one();

    {  // Wait for t2 to signal it's attempting to acquire
      std::unique_lock lock{ sync_mutex };
      sync_cv.wait(lock, [&] { return t2_attempting; });
    }

    // t2 is now blocked on acquire - verify counter hasn't incremented
    CHECK(counter == 1);

    // Release guard (t2 will unblock)
  } };

  std::thread t2{ [&]() {
    {  // Wait for t1 to acquire
      std::unique_lock lock{ sync_mutex };
      sync_cv.wait(lock, [&] { return t1_acquired; });
    }

    {  // Signal that we're about to attempt acquire (will block)
      std::lock_guard lock{ sync_mutex };
      t2_attempting = true;
    }
    sync_cv.notify_one();

    // This blocks until t1 releases
    envy::tui::interactive_mode_guard guard;
    counter++;
    CHECK(counter == 2);
  } };

  t1.join();
  t2.join();

  CHECK(counter == 2);
}

// ============================================================================
// ANSI-aware visible length and padding tests
// ============================================================================

TEST_CASE("calculate_visible_length - plain text") {
  CHECK(envy::tui::test::calculate_visible_length("") == 0);
  CHECK(envy::tui::test::calculate_visible_length("a") == 1);
  CHECK(envy::tui::test::calculate_visible_length("hello") == 5);
  CHECK(envy::tui::test::calculate_visible_length("hello world") == 11);
  CHECK(envy::tui::test::calculate_visible_length("123456789") == 9);
}

TEST_CASE("calculate_visible_length - single ANSI escape") {
  // Red text: ESC[31m
  CHECK(envy::tui::test::calculate_visible_length("\x1b[31m") == 0);
  CHECK(envy::tui::test::calculate_visible_length("\x1b[31mred") == 3);
  CHECK(envy::tui::test::calculate_visible_length("text\x1b[0m") == 4);
  CHECK(envy::tui::test::calculate_visible_length("\x1b[31mred\x1b[0m") == 3);
}

TEST_CASE("calculate_visible_length - multiple ANSI escapes") {
  // Bold red: ESC[1;31m
  CHECK(envy::tui::test::calculate_visible_length("\x1b[1;31mbold red\x1b[0m") == 8);

  // Multiple colors
  CHECK(envy::tui::test::calculate_visible_length("\x1b[31mred\x1b[32mgreen\x1b[0m") == 8);

  // Complex formatting
  std::string complex = "\x1b[1m\x1b[31mBold Red\x1b[0m Normal \x1b[32mGreen\x1b[0m";
  CHECK(envy::tui::test::calculate_visible_length(complex) ==
        21);  // "Bold Red Normal Green"
}

TEST_CASE("calculate_visible_length - only ANSI codes") {
  CHECK(envy::tui::test::calculate_visible_length("\x1b[31m\x1b[0m") == 0);
  CHECK(envy::tui::test::calculate_visible_length("\x1b[1;31;42m") == 0);
}

TEST_CASE("calculate_visible_length - mixed content") {
  // Progress bar with colors: [[package]] 50%
  std::string colored = "\x1b[1m[[package]]\x1b[0m \x1b[32m50%\x1b[0m";
  CHECK(envy::tui::test::calculate_visible_length(colored) == 15);  // "[[package]] 50%"

  // Real-world TUI output
  std::string progress = "[[arm.gcc@v2]] \x1b[32mBuilding...\x1b[0m [=====>    ] 50.0%";
  CHECK(envy::tui::test::calculate_visible_length(progress) == 45);
}

TEST_CASE("calculate_visible_length - incomplete ANSI sequence") {
  // Incomplete sequences stay in escape mode (waiting for 'm' terminator)
  CHECK(envy::tui::test::calculate_visible_length("\x1b[") ==
        0);  // Started but not finished
  CHECK(envy::tui::test::calculate_visible_length("\x1b[31") ==
        0);  // Still in escape, no 'm' yet
  CHECK(envy::tui::test::calculate_visible_length("text\x1b[") ==
        4);  // "text" visible, then incomplete escape
}

TEST_CASE("calculate_visible_length - ESC without bracket") {
  // ESC character without [ should count ESC as visible
  CHECK(envy::tui::test::calculate_visible_length("\x1b"
                                                  "text") == 5);  // ESC + "text"
  CHECK(envy::tui::test::calculate_visible_length("a\x1b"
                                                  "b") == 3);  // "a" + ESC + "b"
}

TEST_CASE("calculate_visible_length - unicode and special chars") {
  // Regular ASCII punctuation and symbols
  CHECK(envy::tui::test::calculate_visible_length("[](){}<>") == 8);
  CHECK(envy::tui::test::calculate_visible_length("!@#$%^&*") == 8);
  CHECK(envy::tui::test::calculate_visible_length("  spaces  ") == 10);
  CHECK(envy::tui::test::calculate_visible_length("\t\ttabs\t") ==
        28);  // tabs count as 8 columns: 8+8+4+8=28
}

TEST_CASE("pad_to_width - plain text shorter than width") {
  CHECK(envy::tui::test::pad_to_width("hello", 10) == "hello     ");
  CHECK(envy::tui::test::pad_to_width("a", 5) == "a    ");
  CHECK(envy::tui::test::pad_to_width("", 3) == "   ");
}

TEST_CASE("pad_to_width - plain text equal to width") {
  CHECK(envy::tui::test::pad_to_width("hello", 5) == "hello");
  CHECK(envy::tui::test::pad_to_width("exact", 5) == "exact");
}

TEST_CASE("pad_to_width - plain text longer than width") {
  // Now truncates to fit within width
  CHECK(envy::tui::test::pad_to_width("hello world", 5) == "hello");
  CHECK(envy::tui::test::pad_to_width("toolong", 3) == "too");
}

TEST_CASE("pad_to_width - width zero and negative") {
  // Zero or negative width truncates to empty (nothing fits)
  CHECK(envy::tui::test::pad_to_width("text", 0) == "");
  CHECK(envy::tui::test::pad_to_width("text", -5) == "");
  CHECK(envy::tui::test::pad_to_width("", 0) == "");
}

TEST_CASE("pad_to_width - ANSI colored text") {
  std::string red = "\x1b[31mred\x1b[0m";
  std::string padded = envy::tui::test::pad_to_width(red, 10);

  // Should be: "\x1b[31mred\x1b[0m       " (3 visible chars + 7 spaces)
  CHECK(padded.size() == red.size() + 7);
  CHECK(envy::tui::test::calculate_visible_length(padded) == 10);
  CHECK(padded.starts_with("\x1b[31mred\x1b[0m"));
  CHECK(padded.ends_with("       "));
}

TEST_CASE("pad_to_width - multiple ANSI sequences") {
  std::string multicolor = "\x1b[31mred\x1b[32mgreen\x1b[0m";  // visible: "redgreen" = 8
  std::string padded = envy::tui::test::pad_to_width(multicolor, 12);

  CHECK(envy::tui::test::calculate_visible_length(padded) == 12);
  CHECK(padded.starts_with(multicolor));
  CHECK(padded.ends_with("    "));  // 4 spaces
}

TEST_CASE("pad_to_width - complex formatting") {
  // Bold red text: ESC[1;31m ... ESC[0m
  std::string formatted = "\x1b[1;31mBold\x1b[0m";  // visible: "Bold" = 4
  std::string padded = envy::tui::test::pad_to_width(formatted, 10);

  CHECK(envy::tui::test::calculate_visible_length(padded) == 10);
  CHECK(padded.starts_with(formatted));
}

TEST_CASE("pad_to_width - only ANSI codes") {
  std::string only_ansi = "\x1b[31m\x1b[0m";  // visible: 0
  std::string padded = envy::tui::test::pad_to_width(only_ansi, 5);

  CHECK(envy::tui::test::calculate_visible_length(padded) == 5);
  CHECK(padded == only_ansi + "     ");
}

TEST_CASE("pad_to_width - real-world progress bar") {
  // Simulate: [[package]] Building... [=====>    ] 50.0%
  std::string bar = "\x1b[1m[[pkg]]\x1b[0m Build [==>  ] \x1b[32m50%\x1b[0m";
  // Visible: "[[pkg]] Build [==>  ] 50%" = 25 chars (7 + 15 + 3)

  int visible = envy::tui::test::calculate_visible_length(bar);
  CHECK(visible == 25);

  std::string padded = envy::tui::test::pad_to_width(bar, 80);
  CHECK(envy::tui::test::calculate_visible_length(padded) == 80);
  CHECK(padded.starts_with(bar));
  CHECK(padded.size() == bar.size() + (80 - 25));
}

TEST_CASE("pad_to_width - nested ANSI sequences") {
  // Some terminals support nested formatting
  std::string nested = "\x1b[1m\x1b[31mbold red\x1b[0m\x1b[0m";  // visible: "bold red" = 8
  std::string padded = envy::tui::test::pad_to_width(nested, 15);

  CHECK(envy::tui::test::calculate_visible_length(padded) == 15);
  CHECK(padded.starts_with(nested));
  CHECK(padded.ends_with("       "));  // 7 spaces
}

TEST_CASE("pad_to_width - interleaved text and ANSI") {
  std::string interleaved = "a\x1b[31mb\x1b[0mc\x1b[32md\x1b[0me";  // visible: "abcde" = 5
  std::string padded = envy::tui::test::pad_to_width(interleaved, 10);

  CHECK(envy::tui::test::calculate_visible_length(padded) == 10);
  CHECK(padded.starts_with(interleaved));
  CHECK(padded.ends_with("     "));
}

TEST_CASE("pad_to_width - edge case empty with width") {
  std::string padded = envy::tui::test::pad_to_width("", 5);
  CHECK(padded == "     ");
  CHECK(envy::tui::test::calculate_visible_length(padded) == 5);
}

TEST_CASE("pad_to_width - preserves exact ANSI codes") {
  // Verify that padding doesn't modify the original ANSI content
  std::string original = "\x1b[38;5;214mOrange\x1b[0m";  // 256-color orange
  std::string padded = envy::tui::test::pad_to_width(original, 15);

  CHECK(padded.substr(0, original.size()) == original);
  CHECK(envy::tui::test::calculate_visible_length(padded) == 15);
}

TEST_CASE("calculate_visible_length - stress test long string") {
  // Build a long string with many ANSI sequences
  std::string long_str;
  for (int i = 0; i < 100; ++i) {
    long_str += "\x1b[31m" + std::string(10, 'a' + (i % 26)) + "\x1b[0m";
  }
  // Expected: 100 * 10 = 1000 visible chars
  CHECK(envy::tui::test::calculate_visible_length(long_str) == 1000);

  std::string padded = envy::tui::test::pad_to_width(long_str, 1200);
  CHECK(envy::tui::test::calculate_visible_length(padded) == 1200);
}

TEST_CASE("pad_to_width - idempotent when already at width") {
  std::string text = "exactly ten!";  // 12 chars
  std::string first_pad = envy::tui::test::pad_to_width(text, 12);
  std::string second_pad = envy::tui::test::pad_to_width(first_pad, 12);

  CHECK(first_pad == text);
  CHECK(second_pad == text);
  CHECK(first_pad == second_pad);
}

TEST_CASE("calculate_visible_length - all escape sequences end with 'm'") {
  // Verify we correctly identify 'm' as terminator for all SGR sequences
  std::vector<std::string> sequences = {
    "\x1b[0m",               // Reset
    "\x1b[1m",               // Bold
    "\x1b[2m",               // Dim
    "\x1b[3m",               // Italic
    "\x1b[4m",               // Underline
    "\x1b[31m",              // Red
    "\x1b[1;31m",            // Bold red
    "\x1b[38;5;214m",        // 256-color
    "\x1b[38;2;255;128;0m",  // RGB color
  };

  for (auto const &seq : sequences) {
    CHECK(envy::tui::test::calculate_visible_length(seq) == 0);
    CHECK(envy::tui::test::calculate_visible_length(seq + "text") == 4);
  }
}

// ============================================================================
// ANSI-aware truncation tests
// ============================================================================

TEST_CASE("truncate_to_width_ansi_aware - plain text shorter than width") {
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("hello", 10) == "hello");
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("ab", 5) == "ab");
}

TEST_CASE("truncate_to_width_ansi_aware - plain text exact width") {
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("hello", 5) == "hello");
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("12345", 5) == "12345");
}

TEST_CASE("truncate_to_width_ansi_aware - plain text longer than width") {
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("hello world", 5) == "hello");
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("abcdefghij", 3) == "abc");
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("1234567890", 7) == "1234567");
}

TEST_CASE("truncate_to_width_ansi_aware - width zero") {
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("hello", 0) == "");
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("", 0) == "");
}

TEST_CASE("truncate_to_width_ansi_aware - empty string") {
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("", 10) == "");
}

TEST_CASE("truncate_to_width_ansi_aware - ANSI at end preserved") {
  // "hello" (5 chars) + reset code
  std::string s = "hello\x1b[0m";
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 5) == s);
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 10) == s);
}

TEST_CASE("truncate_to_width_ansi_aware - ANSI at start preserved") {
  std::string s = "\x1b[31mhello";  // Red "hello"
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 5) == s);
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 3) == "\x1b[31mhel");
}

TEST_CASE("truncate_to_width_ansi_aware - ANSI in middle preserved") {
  std::string s = "hel\x1b[31mlo world";  // "hel" + red + "lo world"
  std::string result = envy::tui::test::truncate_to_width_ansi_aware(s, 5);
  CHECK(result == "hel\x1b[31mlo");
  CHECK(envy::tui::test::calculate_visible_length(result) == 5);
}

TEST_CASE("truncate_to_width_ansi_aware - multiple ANSI codes") {
  std::string s = "\x1b[1m\x1b[31mBold Red Text\x1b[0m";  // Bold red "Bold Red Text"
  std::string result = envy::tui::test::truncate_to_width_ansi_aware(s, 8);
  CHECK(result == "\x1b[1m\x1b[31mBold Red");
  CHECK(envy::tui::test::calculate_visible_length(result) == 8);
}

TEST_CASE("truncate_to_width_ansi_aware - truncate with ANSI code") {
  std::string s = "hello\x1b[31m world";  // "hello" + red + " world"
  // Truncate to 5: includes ANSI after "hello"
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 5) == "hello\x1b[31m");
  // Truncate to 6: includes ANSI + space
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 6) == "hello\x1b[31m ");
}

TEST_CASE("truncate_to_width_ansi_aware - very long line") {
  std::string long_line =
      "[[local.armgcc@r0]] 100% [====================] 276.57MB/276.57MB "
      "arm-gnu-toolchain-14.3.rel1-mingw-w64-x86_64-arm-none-eabi.zip";
  int width = 80;
  std::string result = envy::tui::test::truncate_to_width_ansi_aware(long_line, width);
  CHECK(envy::tui::test::calculate_visible_length(result) == width);
  CHECK(result.size() <= long_line.size());
}

TEST_CASE("truncate_to_width_ansi_aware - colored very long line") {
  std::string s =
      "\x1b[1m[[local.armgcc@r0]]\x1b[0m 100% "
      "[====================] 276.57MB/276.57MB "
      "\x1b[32marm-gnu-toolchain.zip\x1b[0m extra text";
  int width = 60;
  std::string result = envy::tui::test::truncate_to_width_ansi_aware(s, width);
  CHECK(envy::tui::test::calculate_visible_length(result) == width);
}

TEST_CASE("truncate_to_width_ansi_aware - only ANSI codes") {
  std::string s = "\x1b[31m\x1b[1m\x1b[0m";
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 5) == s);
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 0) == "");
}

TEST_CASE("truncate_to_width_ansi_aware - complete ANSI after truncation point") {
  // Complete ANSI sequences after truncation point should be included
  std::string s = "hello\x1b[31m world";  // "hello" + red code + " world"
  std::string result = envy::tui::test::truncate_to_width_ansi_aware(s, 5);
  // Should include the complete ANSI sequence after "hello"
  CHECK(result == "hello\x1b[31m");
  CHECK(envy::tui::test::calculate_visible_length(result) == 5);
}

TEST_CASE("pad_to_width - now truncates long lines") {
  // After our fix, pad_to_width should truncate lines that are too long
  std::string long_line = "This is a very long line that exceeds the terminal width";
  std::string result = envy::tui::test::pad_to_width(long_line, 20);
  CHECK(envy::tui::test::calculate_visible_length(result) == 20);
  CHECK(result == "This is a very long ");
}

TEST_CASE("pad_to_width - truncates colored long lines") {
  std::string s = "\x1b[31mThis is a very long colored line\x1b[0m that exceeds width";
  std::string result = envy::tui::test::pad_to_width(s, 15);
  CHECK(envy::tui::test::calculate_visible_length(result) == 15);
}

TEST_CASE("calculate_visible_length - handles tabs") {
  // Tabs count as 8 columns
  CHECK(envy::tui::test::calculate_visible_length("\t") == 8);
  CHECK(envy::tui::test::calculate_visible_length("a\tb") ==
        10);  // a + tab + b = 1 + 8 + 1
  CHECK(envy::tui::test::calculate_visible_length("\t\t") == 16);
  CHECK(envy::tui::test::calculate_visible_length("hello\tworld") ==
        18);  // 5 + 8 + 5 = 18
}

TEST_CASE("truncate_to_width_ansi_aware - handles tabs") {
  // Tab counts as 8 columns, so should be truncated
  std::string s = "hello\tworld";  // 5 + 8 + 5 = 18 columns
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 13) == "hello\t");  // 5 + 8 = 13
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 10) ==
        "hello");  // Tab would exceed, so truncate before it
  CHECK(envy::tui::test::truncate_to_width_ansi_aware(s, 5) == "hello");
  CHECK(envy::tui::test::truncate_to_width_ansi_aware("a\tb", 5) ==
        "a");  // 'a' fits, tab would exceed
}

TEST_CASE("pad_to_width - handles tabs") {
  // Tabs count as 8 columns
  std::string s = "a\tb";  // 1 + 8 + 1 = 10 columns
  CHECK(envy::tui::test::calculate_visible_length(envy::tui::test::pad_to_width(s, 10)) ==
        10);
  CHECK(envy::tui::test::pad_to_width(s, 10) == "a\tb");  // Exactly fits

  // Tab makes line too long, should truncate to just "a" then pad to 5
  std::string result = envy::tui::test::pad_to_width(s, 5);
  CHECK(result == "a    ");  // 'a' + 4 spaces = 5 visible chars
  CHECK(envy::tui::test::calculate_visible_length(result) == 5);
}

#endif  // ENVY_UNIT_TEST
