#include "shell.h"

#include "doctest.h"

#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(_WIN32)

TEST_CASE("shell_run throws on Windows") {
  envy::shell_invocation inv;
  inv.on_output_line = [](std::string_view) {};

  CHECK_THROWS_AS(envy::shell_run("echo hi", inv), std::runtime_error);
}

#else

namespace {

envy::shell_env_t default_env() {
  auto env{ envy::shell_getenv() };
  REQUIRE(!env.empty());
  return env;
}

std::vector<std::string> run_collect(std::string_view script,
                                     std::optional<fs::path> cwd = std::nullopt,
                                     envy::shell_env_t env = default_env()) {
  std::vector<std::string> lines;
  envy::shell_invocation inv{
    .cwd = cwd,
    .env = std::move(env),
    .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
  };
  auto const exit_code{ envy::shell_run(script, inv) };
  REQUIRE(exit_code == 0);
  return lines;
}

std::string random_suffix() {
  static std::mt19937_64 rng{ std::random_device{}() };
  std::uniform_int_distribution<uint64_t> dist;
  auto value{ dist(rng) };
  std::ostringstream oss;
  oss << std::hex << value;
  return oss.str();
}

}  // namespace

TEST_CASE("shell_getenv captures PATH") {
  auto env{ envy::shell_getenv() };
  REQUIRE(!env.empty());
  CHECK(env.contains("PATH"));
}

TEST_CASE("shell_run executes multiple lines") {
  auto lines{ run_collect("echo first\nprintf 'second\\n'\n") };
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "first");
  CHECK(lines[1] == "second");
}

TEST_CASE("shell_run exposes custom environment variables") {
  auto env{ default_env() };
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

TEST_CASE("shell_run respects working directory") {
  auto temp_dir{ fs::temp_directory_path() / ("envy-shell-test-" + random_suffix()) };
  fs::create_directory(temp_dir);
  auto lines{ run_collect("pwd", temp_dir) };
  REQUIRE(lines.size() == 1);
  auto expected{ fs::weakly_canonical(temp_dir) };
  auto actual{ fs::weakly_canonical(fs::path{ lines[0] }) };
  CHECK(actual == expected);
  fs::remove(temp_dir);
}

TEST_CASE("shell_run surfaces non-zero exit codes") {
  envy::shell_invocation inv{
    .cwd = std::nullopt,
    .env = default_env(),
    .on_output_line = [](std::string_view) {},
  };
  auto const code{ envy::shell_run("exit 7", inv) };
  CHECK(code == 7);
}

TEST_CASE("shell_run delivers trailing partial lines") {
  auto lines{ run_collect("printf 'without-newline'") };
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "without-newline");
}

#endif  // !_WIN32
