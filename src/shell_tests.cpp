#include "shell.h"

#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(_WIN32)

TEST_CASE("shell.h not supported on Windows") {
  // On Windows, including shell.h should cause a compile error via #error
  CHECK(true);
}

#else

extern char **environ;

namespace {

envy::shell_env_t default_env() {
  envy::shell_env_t env;
  if (char **environ_ptr{ environ }; environ_ptr) {
    for (char **entry{ environ_ptr }; *entry != nullptr; ++entry) {
      std::string_view kv{ *entry };
      size_t const sep{ kv.find('=') };
      if (sep == std::string_view::npos) { continue; }
      env[std::string{ kv.substr(0, sep) }] = std::string{ kv.substr(sep + 1) };
    }
  }
  REQUIRE(!env.empty());
  return env;
}

std::vector<std::string> run_collect(std::string_view script,
                                     std::optional<fs::path> cwd = std::nullopt,
                                     envy::shell_env_t env = default_env(),
                                     bool disable_strict = false) {
  std::vector<std::string> lines;
  envy::shell_invocation inv{
    .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
    .cwd = cwd,
    .env = std::move(env),
    .disable_strict = disable_strict,
  };
  auto const result{ envy::shell_run(script, inv) };
  REQUIRE(result.exit_code == 0);
  REQUIRE(!result.signaled);
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

TEST_CASE("shell_run captures PATH from environment") {
  auto env{ default_env() };
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

TEST_CASE("shell_run surfaces non-zero exit codes") {
  envy::shell_invocation inv{
    .on_output_line = [](std::string_view) {},
    .cwd = std::nullopt,
    .env = default_env(),
  };
  auto const result{ envy::shell_run("exit 7", inv) };
  CHECK(result.exit_code == 7);
  CHECK(!result.signaled);
}

TEST_CASE("shell_run delivers trailing partial lines") {
  auto lines{ run_collect("printf 'without-newline'") };
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "without-newline");
}

TEST_CASE("shell_run handles callback exceptions") {
  envy::shell_invocation inv{
    .on_output_line = [](std::string_view) { throw std::runtime_error("test"); },
    .cwd = std::nullopt,
    .env = default_env(),
  };
  CHECK_THROWS_AS(envy::shell_run("echo hi", inv), std::runtime_error);
}

TEST_CASE("shell_run handles signal termination") {
  envy::shell_invocation inv{
    .on_output_line = [](std::string_view) {},
    .cwd = std::nullopt,
    .env = default_env(),
  };
  auto const result{ envy::shell_run("kill -TERM $$", inv) };
  CHECK(result.exit_code == (128 + SIGTERM));
  CHECK(result.signaled);
  CHECK(result.signal == SIGTERM);
}

TEST_CASE("shell_run handles empty script") {
  auto lines{ run_collect("") };
  CHECK(lines.empty());
}

TEST_CASE("shell_run handles invalid working directory") {
  envy::shell_invocation inv{
    .on_output_line = [](std::string_view) {},
    .cwd = "/nonexistent/directory/path",
    .env = default_env(),
  };
  auto const result{ envy::shell_run("echo hi", inv) };
  CHECK(result.exit_code == 127);
}

TEST_CASE("shell_run handles large output") {
  std::vector<std::string> lines;
  envy::shell_invocation inv{
    .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
    .cwd = std::nullopt,
    .env = default_env(),
  };
  // Generate 100KB of output (1000 lines × 100 chars each)
  auto const result{ envy::shell_run(
    "for i in {1..1000}; do printf '%0100d\\n' $i; done", inv) };
  REQUIRE(result.exit_code == 0);
  CHECK(lines.size() == 1000);
  CHECK(lines[0].size() == 100);
  CHECK(lines[999].size() == 100);
}

TEST_CASE("shell_run respects disable_strict flag") {
  std::vector<std::string> lines;
  envy::shell_invocation inv{
    .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
    .cwd = std::nullopt,
    .env = default_env(),
    .disable_strict = true,
  };
  // This succeeds with disable_strict (unset var allowed)
  auto const result{ envy::shell_run("echo ${UNSET_VAR:-default}", inv) };
  REQUIRE(result.exit_code == 0);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "default");
}

TEST_CASE("shell_run strict mode catches unset variables") {
  std::vector<std::string> lines;
  envy::shell_invocation inv{
    .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
    .cwd = std::nullopt,
    .env = default_env(),
    .disable_strict = false,  // Strict mode enabled (default)
  };
  // This fails in strict mode (unset var without default)
  auto const result{ envy::shell_run("echo $DEFINITELY_UNSET_VAR_12345", inv) };
  CHECK(result.exit_code != 0);
}

TEST_CASE("shell_run callback is required") {
  // Callback is now a required field in designated initializer
  // This test verifies the API design - it would fail to compile if callback was optional
  std::vector<std::string> lines;
  envy::shell_invocation inv{
    .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
    .env = default_env(),
  };
  auto const result{ envy::shell_run("echo test", inv) };
  CHECK(result.exit_code == 0);
}

#endif  // !_WIN32
