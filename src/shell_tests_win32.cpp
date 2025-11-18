#if defined(_WIN32)
#include "shell.h"
#include "doctest.h"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<std::string> run_collect(
    std::string_view script,
    envy::shell_choice shell = envy::shell_choice::powershell,
    std::optional<fs::path> cwd = std::nullopt,
    envy::shell_env_t env = envy::shell_getenv()) {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line =
                               [&](std::string_view line) {
                                 // Strip C0 control character stream tags for test assertions
                                 if (!line.empty() && (line[0] == '\x1C' || line[0] == '\x1D' ||
                                                       line[0] == '\x1E' || line[0] == '\x1F')) {
                                   lines.emplace_back(line.substr(1));
                                 } else {
                                   lines.emplace_back(line);
                                 }
                               },
                           .cwd = std::move(cwd),
                           .env = std::move(env),
                           .shell = shell };
  auto const result{ envy::shell_run(script, inv) };
  REQUIRE(result.exit_code == 0);
  REQUIRE(!result.signal.has_value());
  return lines;
}

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
  struct dir_cleanup { fs::path path; ~dir_cleanup(){ std::error_code ec; fs::remove_all(path, ec);} } cleanup{ tmp_dir };
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

TEST_CASE("shell_run captures all PowerShell streams without hanging (powershell)") {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::powershell };

  // Test Write-Host (information stream) → 0x1E (RS)
  lines.clear();
  auto result{ envy::shell_run("Write-Host 'test host output'", inv) };
  CHECK(result.exit_code == 0);
  CHECK(lines.size() > 0);
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.starts_with('\x1E') && l.find("test host output") != std::string::npos;
  }));

  // Test Write-Warning → 0x1D (GS)
  lines.clear();
  result = envy::shell_run("Write-Warning 'test warning'", inv);
  CHECK(result.exit_code == 0);
  CHECK(lines.size() > 0);
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.starts_with('\x1D') && l.find("test warning") != std::string::npos;
  }));

  // Test Write-Error (non-terminating) → 0x1C (FS)
  lines.clear();
  result = envy::shell_run("Write-Error 'test error' -ErrorAction Continue; Write-Output 'continued'", inv);
  CHECK(result.exit_code == 0);
  CHECK(lines.size() >= 2);
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.starts_with('\x1C') && l.find("test error") != std::string::npos;
  }));
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.find("continued") != std::string::npos;
  }));

  // Test Write-Verbose → 0x1F (US)
  lines.clear();
  result = envy::shell_run("Write-Verbose 'test verbose' -Verbose", inv);
  CHECK(result.exit_code == 0);
  CHECK(lines.size() > 0);
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.starts_with('\x1F') && l.find("test verbose") != std::string::npos;
  }));

  // Test Write-Debug → 0x1F (US)
  lines.clear();
  result = envy::shell_run("Write-Debug 'test debug' -Debug", inv);
  CHECK(result.exit_code == 0);
  CHECK(lines.size() > 0);
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.starts_with('\x1F') && l.find("test debug") != std::string::npos;
  }));

  // Test Get-ChildItem (produces lots of output, potential hang)
  lines.clear();
  result = envy::shell_run("Get-ChildItem | Select-Object -First 5", inv);
  CHECK(result.exit_code == 0);
  // Should complete without hanging
}

TEST_CASE("shell_run PowerShell streams with explicit exit codes (powershell)") {
  std::vector<std::string> lines;
  envy::shell_run_cfg inv{ .on_output_line = [&](std::string_view line) { lines.emplace_back(line); },
                           .cwd = std::nullopt,
                           .env = envy::shell_getenv(),
                           .shell = envy::shell_choice::powershell };

  // Warning + explicit exit should propagate exit code
  lines.clear();
  auto result{ envy::shell_run("Write-Warning 'warn'; exit 42", inv) };
  CHECK(result.exit_code == 42);
  CHECK(std::any_of(lines.begin(), lines.end(), [](auto const &l) {
    return l.starts_with('\x1D');  // 0x1D = GS (Warning)
  }));
}

#endif // _WIN32
