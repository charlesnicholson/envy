#if !defined(_WIN32)
#include "shell.h"
#include "doctest.h"
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<std::string> run_collect(std::string_view script,
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

TEST_CASE("shell_parse_choice POSIX supports bash and sh") {
  CHECK(envy::shell_parse_choice(std::nullopt) == envy::shell_choice::bash);
  CHECK(envy::shell_parse_choice("bash") == envy::shell_choice::bash);
  CHECK(envy::shell_parse_choice("sh") == envy::shell_choice::sh);
  CHECK_THROWS_AS(envy::shell_parse_choice("powershell"), std::invalid_argument);
}

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
  auto const result{ envy::shell_run("for i in {1..1000}; do printf '%0100d\\n' $i; done",
                                     inv) };
  REQUIRE(result.exit_code == 0);
  CHECK(lines.size() == 1000);
  CHECK(lines[0].size() == 100);
  CHECK(lines[999].size() == 100);
}

TEST_CASE("shell_run callback is required") {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) { lines.emplace_back(line); },
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::bash };
  auto const result{ envy::shell_run("echo test", inv) };
  CHECK(result.exit_code == 0);
}

#endif // !_WIN32
