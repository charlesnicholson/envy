#include "engine.h"
#include "lua_ctx_bindings.h"
#include "lua_envy.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "sol_util.h"

#include "doctest.h"

#include <filesystem>
#include <memory>
#include <ranges>
#include <vector>

namespace {

struct ctx_run_fixture {
  envy::recipe_spec *spec{ nullptr };
  std::unique_ptr<envy::recipe> recipe_ptr;
  envy::lua_ctx_common ctx{};
  envy::sol_state_ptr lua;
  std::filesystem::path tmp_dir;

  ctx_run_fixture()
      : tmp_dir{ std::filesystem::temp_directory_path() / "envy_ctx_run_test" } {
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    spec = envy::recipe_spec::pool()->emplace("test.run@v1",
                                              envy::recipe_spec::weak_ref{},
                                              "{}",
                                              std::nullopt,
                                              nullptr,
                                              nullptr,
                                              std::vector<envy::recipe_spec *>{},
                                              std::nullopt,
                                              std::filesystem::path{});

    recipe_ptr = std::unique_ptr<envy::recipe>(new envy::recipe{
        .key = envy::recipe_key(*spec),
        .spec = spec,
        .exec_ctx = nullptr,
        .lua = envy::sol_state_ptr{},
        .lock = nullptr,
        .declared_dependencies = {},
        .owned_dependency_specs = {},
        .dependencies = {},
        .product_dependencies = {},
        .weak_references = {},
        .products = {},
        .resolved_weak_dependency_keys = {},
        .canonical_identity_hash = {},
        .asset_path = std::filesystem::path{},
        .recipe_file_path = std::nullopt,
        .result_hash = {},
        .type = envy::recipe_type::UNKNOWN,
        .cache_ptr = nullptr,
        .default_shell_ptr = nullptr,
    });

    ctx.run_dir = tmp_dir;
    ctx.engine_ = nullptr;
    ctx.recipe_ = recipe_ptr.get();

    lua = envy::sol_util_make_lua_state();
    envy::lua_envy_install(*lua);
    (*lua)["run"] = envy::make_ctx_run(&ctx);
  }

  ~ctx_run_fixture() { std::filesystem::remove_all(tmp_dir); }
};

}  // namespace

TEST_CASE_FIXTURE(ctx_run_fixture,
                  "ctx.run returns only exit_code when capture is false") {
  std::string const cmd{ "python3 -c \"import sys; sys.stdout.write('ok')\"" };
  sol::function run_fn{ (*lua)["run"] };
  sol::table tbl = run_fn(cmd);
  CHECK(tbl.get<int>("exit_code") == 0);
  CHECK_FALSE(tbl["stdout"].valid());
  CHECK_FALSE(tbl["stderr"].valid());
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run capture option returns stdout and stderr") {
  std::string const cmd{
    "python3 -c \"import sys; sys.stdout.write('out\\\\n'); sys.stderr.write('err\\\\n')\""
  };
  sol::function run_fn{ (*lua)["run"] };
  sol::table opts{ lua->create_table() };
  opts["capture"] = true;
  sol::table tbl = run_fn(cmd, opts);
  CHECK(tbl.get<int>("exit_code") == 0);
  CHECK(tbl.get<std::string>("stdout") == "out\n");
  CHECK(tbl.get<std::string>("stderr") == "err\n");
}

TEST_CASE_FIXTURE(ctx_run_fixture, "ctx.run captures large stdout/stderr without loss") {
  // Emit ~2MB per stream with distinct ranges to stress pipe handling.
  constexpr long long start_out{ 10'000'000'000LL };
  constexpr long long start_err{ 20'000'000'000LL };
  constexpr int count{ 180'000 };  // ~2.1MB per stream at ~12 bytes per line

  std::filesystem::path const script_rel{ "test_data/ctx_run_stress.py" };
  std::filesystem::path const script_abs{ std::filesystem::absolute(script_rel) };
  std::string script{ "python3 \"" + script_abs.generic_string() + "\"" };

  sol::function run_fn{ (*lua)["run"] };
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
