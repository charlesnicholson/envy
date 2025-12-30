#include "engine.h"
#include "lua_ctx_bindings.h"
#include "lua_envy.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "sol_util.h"

#include "doctest.h"

#include <filesystem>
#include <memory>
#include <ranges>
#include <vector>

namespace {

#if defined(_WIN32)
constexpr char const *kPythonCmd = "py -3";
#else
constexpr char const *kPythonCmd = "python3";
#endif

struct ctx_run_fixture {
  envy::pkg_cfg *cfg{ nullptr };
  std::unique_ptr<envy::pkg> pkg_ptr;
  envy::lua_ctx_common ctx{};
  envy::sol_state_ptr lua;
  std::filesystem::path tmp_dir;

  ctx_run_fixture()
      : tmp_dir{ std::filesystem::temp_directory_path() / "envy_ctx_run_test" } {
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    cfg = envy::pkg_cfg::pool()->emplace("test.run@v1",
                                         envy::pkg_cfg::weak_ref{},
                                         "{}",
                                         std::nullopt,
                                         nullptr,
                                         nullptr,
                                         std::vector<envy::pkg_cfg *>{},
                                         std::nullopt,
                                         std::filesystem::path{});

    pkg_ptr = std::unique_ptr<envy::pkg>(new envy::pkg{
        .key = envy::pkg_key(*cfg),
        .cfg = cfg,
        .cache_ptr = nullptr,
        .default_shell_ptr = nullptr,
        .exec_ctx = nullptr,
        .lua = envy::sol_state_ptr{},
        .lock = nullptr,
        .canonical_identity_hash = {},
        .pkg_path = std::filesystem::path{},
        .spec_file_path = std::nullopt,
        .result_hash = {},
        .type = envy::pkg_type::UNKNOWN,
        .declared_dependencies = {},
        .owned_dependency_cfgs = {},
        .dependencies = {},
        .product_dependencies = {},
        .weak_references = {},
        .products = {},
        .resolved_weak_dependency_keys = {},
    });

    ctx.run_dir = tmp_dir;
    ctx.engine_ = nullptr;
    ctx.pkg_ = pkg_ptr.get();

    lua = envy::sol_util_make_lua_state();
    envy::lua_envy_install(*lua);
    (*lua)["run"] = envy::make_ctx_run(&ctx);
  }

  ~ctx_run_fixture() { std::filesystem::remove_all(tmp_dir); }
};

}  // namespace

TEST_CASE_FIXTURE(ctx_run_fixture,
                  "ctx.run returns only exit_code when capture is false") {
  std::string const cmd{ std::string{ kPythonCmd } +
                         " -c \"import sys; sys.stdout.write('ok')\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table tbl = run_fn(cmd);
  CHECK(tbl.get<int>("exit_code") == 0);
  CHECK_FALSE(tbl["stdout"].valid());
  CHECK_FALSE(tbl["stderr"].valid());
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run capture option returns stdout and stderr") {
#if defined(_WIN32)
  // Use PowerShell commands that write to stdout and stderr
  std::string const cmd{ "[Console]::Out.Write('out'); [Console]::Error.Write('err')" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["capture"] = true;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 0);
  std::string stdout_str = tbl.get<std::string>("stdout");
  std::string stderr_str = tbl.get<std::string>("stderr");
  CHECK(stdout_str.find("out") != std::string::npos);
  CHECK(stderr_str.find("err") != std::string::npos);
#else
  std::string const cmd{
    std::string{ kPythonCmd } +
    " -c \"import sys; sys.stdout.write('out\\\\n'); sys.stderr.write('err\\\\n')\""
  };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["capture"] = true;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 0);
  CHECK(tbl.get<std::string>("stdout") == "out\n");
  CHECK(tbl.get<std::string>("stderr") == "err\n");
#endif
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run captures large stdout/stderr without loss") {
  // Emit ~2MB per stream with distinct ranges to stress pipe handling.
  constexpr long long start_out{ 10'000'000'000LL };
  constexpr long long start_err{ 20'000'000'000LL };
  constexpr int count{ 180'000 };  // ~2.1MB per stream at ~12 bytes per line

  std::filesystem::path const script_rel{ "test_data/ctx_run_stress.py" };
  std::filesystem::path const script_abs{ std::filesystem::absolute(script_rel) };
  std::string script{ std::string{ kPythonCmd } + " \"" + script_abs.generic_string() +
                      "\"" };

  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["capture"] = true;
  sol::table tbl = run_fn(script, opts);
  CHECK(tbl.get<int>("exit_code") == 0);

  auto split_lines{ [](std::string const &text) {
    std::vector<std::string> lines;
    for (auto &&segment : text | std::views::split('\n')) {
      lines.emplace_back(std::string_view{ segment.begin(), segment.end() });
    }
    if (!lines.empty() && lines.back().empty()) { lines.pop_back(); }
    return lines;
  } };

  auto stdout_lines{ split_lines(tbl.get<std::string>("stdout")) };
  auto stderr_lines{ split_lines(tbl.get<std::string>("stderr")) };

  REQUIRE(static_cast<int>(stdout_lines.size()) == count);
  REQUIRE(static_cast<int>(stderr_lines.size()) == count);

  CHECK(stdout_lines.front() == std::to_string(start_out));
  CHECK(stdout_lines.back() == std::to_string(start_out + count - 1));
  CHECK(stderr_lines.front() == std::to_string(start_err));
  CHECK(stderr_lines.back() == std::to_string(start_err + count - 1));
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run with check=false allows non-zero exit") {
  std::string const cmd{ std::string{ kPythonCmd } + " -c \"import sys; sys.exit(7)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["check"] = false;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 7);
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run with check=true throws on non-zero exit") {
  std::string const cmd{ std::string{ kPythonCmd } + " -c \"import sys; sys.exit(42)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["check"] = true;
  CHECK_THROWS_WITH_AS(run_fn(cmd, opts),
                       doctest::Contains("exit code 42"),
                       std::runtime_error);
}

TEST_CASE_FIXTURE(ctx_run_fixture,
                  "ctx.run with check=true includes command and output in error") {
  std::string const cmd{ std::string{ kPythonCmd } +
                         " -c \"import sys; print('out'); "
                         "sys.stderr.write('err\\\\n'); sys.exit(13)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["check"] = true;
  try {
    run_fn(cmd, opts);
    FAIL("Expected exception");
  } catch (std::runtime_error const &e) {
    std::string msg{ e.what() };
    CHECK(msg.find("exit code 13") != std::string::npos);
    CHECK(msg.find("Command:") != std::string::npos);
    CHECK(msg.find(kPythonCmd) != std::string::npos);
    CHECK(msg.find("--- stdout ---") != std::string::npos);
    CHECK(msg.find("out") != std::string::npos);
    CHECK(msg.find("--- stderr ---") != std::string::npos);
    CHECK(msg.find("err") != std::string::npos);
  }
}

TEST_CASE_FIXTURE(ctx_run_fixture,
                  "ctx.run with check=false and capture returns exit_code and output") {
  std::string const cmd{ std::string{ kPythonCmd } +
                         " -c \"import sys; print('stdout_data'); "
                         "sys.stderr.write('stderr_data\\\\n'); sys.exit(5)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["check"] = false;
  opts["capture"] = true;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 5);
  CHECK(tbl.get<std::string>("stdout").find("stdout_data") != std::string::npos);
  CHECK(tbl.get<std::string>("stderr").find("stderr_data") != std::string::npos);
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run check defaults to false") {
  std::string const cmd{ std::string{ kPythonCmd } + " -c \"import sys; sys.exit(99)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table tbl = run_fn(cmd);
  CHECK(tbl.get<int>("exit_code") == 99);
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run preserves empty lines in captured output") {
  std::string const cmd{ std::string{ kPythonCmd } +
                         " -c \"print('line1'); print(''); print('line2')\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["capture"] = true;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 0);
  std::string stdout_str = tbl.get<std::string>("stdout");
  // Should have: "line1\n" + "\n" (empty line) + "line2\n"
  CHECK(stdout_str == "line1\n\nline2\n");
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run with interactive=true runs command") {
  std::string const cmd{ std::string{ kPythonCmd } + " -c \"import sys; sys.exit(0)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table opts{ lua->create_table() };
  opts["interactive"] = true;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 0);
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run interactive defaults to false") {
  std::string const cmd{ std::string{ kPythonCmd } + " -c \"import sys; sys.exit(0)\"" };
  sol::function run_fn = (*lua)["run"];
  sol::table tbl = run_fn(cmd);
  CHECK(tbl.get<int>("exit_code") == 0);
}
