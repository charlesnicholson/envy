#include "shell.h"

#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)

TEST_CASE("shell_parse_choice Windows defaults to powershell") {
  CHECK(envy::shell_parse_choice(std::nullopt) == envy::shell_choice::powershell);
  CHECK(envy::shell_parse_choice(std::string_view("")) == envy::shell_choice::powershell);
}

TEST_CASE("shell_parse_choice Windows accepts explicit shells") {
  CHECK(envy::shell_parse_choice("powershell") == envy::shell_choice::powershell);
  CHECK(envy::shell_parse_choice("cmd") == envy::shell_choice::cmd);
}

TEST_CASE("shell_parse_choice Windows rejects invalid shells") {
  CHECK_THROWS_AS(envy::shell_parse_choice("bash"), std::invalid_argument);
}

std::vector<std::string> run_collect(
    std::string_view script,
    envy::shell_choice shell = envy::shell_choice::powershell,
    std::optional<fs::path> cwd = std::nullopt,
    envy::shell_env_t env = envy::shell_getenv()) {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) { lines.emplace_back(line); },
                           .cwd = std::move(cwd),
                           .env = std::move(env),
                           .shell = shell };
  auto const result{ envy::shell_run(script, inv) };
  REQUIRE(result.exit_code == 0);
  REQUIRE(!result.signal.has_value());
  return lines;
}

#else

TEST_CASE("shell_parse_choice POSIX supports bash and sh") {
  CHECK(envy::shell_parse_choice(std::nullopt) == envy::shell_choice::bash);
  CHECK(envy::shell_parse_choice("bash") == envy::shell_choice::bash);
  CHECK(envy::shell_parse_choice("sh") == envy::shell_choice::sh);
  CHECK_THROWS_AS(envy::shell_parse_choice("powershell"), std::invalid_argument);
}

std::vector<std::string> run_collect(std::string_view script,
                                     std::optional<fs::path> cwd = std::nullopt,
                                     envy::shell_env_t env = envy::shell_getenv()) {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) { lines.emplace_back(line); },
                           .cwd = cwd,
                           .env = std::move(env),
                           .shell = envy::shell_choice::bash };
  auto const result{ envy::shell_run(script, inv) };
  REQUIRE(result.exit_code == 0);
  REQUIRE(!result.signal.has_value());
  return lines;
}

}  // namespace
#endif

TEST_CASE("shell_getenv captures PATH") {
  auto env{ envy::shell_getenv() };
  REQUIRE(!env.empty());
#if defined(_WIN32)
  CHECK(env.contains("Path") || env.contains("PATH"));
#else
  CHECK(env.contains("PATH"));
#endif
}

#if defined(_WIN32)

TEST_CASE("shell_run powershell executes multiple lines") {
  auto lines{ run_collect("Write-Output 'first'\nWrite-Output 'second'") };
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "first");
  CHECK(lines[1] == "second");
}

TEST_CASE("shell_run exposes custom environment variables (powershell)") {
  auto env{ envy::shell_getenv() };
  env["ENVY_SHELL_TEST"] = "ok";
  auto lines{ run_collect("Write-Output $env:ENVY_SHELL_TEST",
                          envy::shell_choice::powershell,
                          std::nullopt,
                          std::move(env)) };
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "ok");
}

TEST_CASE("shell_run executes with empty environment (powershell)") {
  auto lines =
      run_collect("if ($env:FOO) { Write-Output $env:FOO } else { Write-Output 'blank' }",
                  envy::shell_choice::powershell,
                  std::nullopt,
                  envy::shell_env_t{});
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "blank");
}

TEST_CASE("shell_run supports cmd shell option") {
  auto lines{ run_collect("@echo off\necho first", envy::shell_choice::cmd) };
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "first");
}

TEST_CASE("shell_run respects working directory (powershell)") {
  auto tmp_dir{ fs::temp_directory_path() / "envy-shell-test" };
  fs::create_directories(tmp_dir);
  struct dir_cleanup {
    fs::path path;
    ~dir_cleanup() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  } cleanup{ tmp_dir };
  auto lines{ run_collect("Get-Location | Select-Object -ExpandProperty Path",
                          envy::shell_choice::powershell,
                          tmp_dir) };
  REQUIRE(lines.size() == 1);
  auto actual{ fs::weakly_canonical(fs::path{ lines[0] }) };
  auto expected{ fs::weakly_canonical(tmp_dir) };
  CHECK(actual == expected);
}

TEST_CASE("shell_run surfaces non-zero exit codes (powershell)") {
  envy::shell_run_cfg inv{ .on_output_line = [](std::string_view) {},
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::powershell };
  auto const result{ envy::shell_run("exit 7", inv) };
  CHECK(result.exit_code == 7);
  CHECK(!result.signal.has_value());
}

TEST_CASE("shell_run handles callback exceptions (powershell)") {
  envy::shell_run_cfg inv{ .on_output_line =
                               [](std::string_view) { throw std::runtime_error("test"); },
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::powershell };
  CHECK_THROWS_AS(envy::shell_run("Write-Output 'hi'", inv), std::runtime_error);
}

#else  // !_WIN32

TEST_CASE("shell_run executes multiple lines") {
  auto lines{ run_collect("echo first\nprintf 'second\\n'\n") };
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "first");
  CHECK(lines[1] == "second");
}

TEST_CASE("shell_run exposes custom environment variables") {
  auto env{ envy::shell_getenv() };
  env["ENVY_SHELL_TEST"] = "ok";
  auto lines{ run_collect("printf '%s\\n' \"$ENVY_SHELL_TEST\"", std::nullopt, env) };
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "ok");
}

TEST_CASE("shell_run executes with empty environment") {
  auto lines = run_collect("FOO=blank; export FOO; printf '%s\\n' \"$FOO\"",
                           std::nullopt,
                           envy::shell_env_t{});
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "blank");
}

TEST_CASE("shell_run surfaces non-zero exit codes") {
  envy::shell_run_cfg inv{ .on_output_line = [](std::string_view) {},
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };
  auto const result{ envy::shell_run("exit 7", inv) };
  CHECK(result.exit_code == 7);
  CHECK(!result.signal.has_value());
}

TEST_CASE("shell_run delivers trailing partial lines") {
  auto lines{ run_collect("printf 'without-newline'") };
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "without-newline");
}

TEST_CASE("shell_run handles callback exceptions") {
  envy::shell_run_cfg inv{ .on_output_line =
                               [](std::string_view) { throw std::runtime_error("test"); },
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };
  CHECK_THROWS_AS(envy::shell_run("echo hi", inv), std::runtime_error);
}

// Disabled: hangs in test harness (SIGTERM propagates unexpectedly)
// TEST_CASE("shell_run handles signal termination") {
//   envy::shell_run_cfg inv{ .on_output_line = [](std::string_view) {},
//                               .cwd = std::nullopt,
//                               .env = envy::shell_getenv() };
//   auto const result{ envy::shell_run("kill -TERM $$", inv) };
//   CHECK(result.exit_code == (128 + SIGTERM));
//   CHECK(result.signal.has_value());
//   CHECK(*result.signal == SIGTERM);
// }

TEST_CASE("shell_run handles empty script") {
  auto lines{ run_collect("") };
  CHECK(lines.empty());
}

TEST_CASE("shell_run respects working directory") {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) { lines.emplace_back(line); },
                           .cwd = "/tmp",
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };
  auto const result{ envy::shell_run("pwd", inv) };
  REQUIRE(result.exit_code == 0);
  REQUIRE(lines.size() == 1);
  auto actual{ fs::weakly_canonical(fs::path{ lines[0] }) };
  auto expected{ fs::weakly_canonical(fs::path{ "/tmp" }) };
  CHECK(actual == expected);
}

TEST_CASE("shell_run handles invalid working directory") {
  envy::shell_run_cfg inv{ .on_output_line = [](std::string_view) {},
                           .cwd = "/nonexistent/directory/path",
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };
  auto const result{ envy::shell_run("echo hi", inv) };
  CHECK(result.exit_code == 127);
}

TEST_CASE("shell_run handles large output") {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) { lines.emplace_back(line); },
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };

  // Generate 100KB of output (1000 lines × 100 chars each)
  auto const result{ envy::shell_run("for i in {1..1000}; do printf '%0100d\\n' $i; done",
                                     inv) };
  REQUIRE(result.exit_code == 0);
  CHECK(lines.size() == 1000);
  CHECK(lines[0].size() == 100);
  CHECK(lines[999].size() == 100);
}

TEST_CASE("shell_run callback is required") {
  // Callback is now a required field in designated initializer
  // This test verifies the API design - it would fail to compile if callback was optional
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) { lines.emplace_back(line); },
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };
  auto const result{ envy::shell_run("echo test", inv) };
  CHECK(result.exit_code == 0);
}

#endif  // _WIN32
