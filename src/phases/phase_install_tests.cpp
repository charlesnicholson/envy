#include "phase_install.h"

#include "cache.h"
#include "engine.h"
#include "lua_envy.h"
#include "phase_check.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "sol_util.h"

#include "doctest.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

namespace envy {

namespace {

// Fixture for testing install phase with temporary cache
struct install_test_fixture {
  recipe_spec *spec;
  std::unique_ptr<recipe> r;
  std::filesystem::path temp_root;
  cache test_cache;
  engine eng;

  install_test_fixture()
      : temp_root{ []() {
          static std::mt19937_64 rng{ std::random_device{}() };
          auto suffix = std::to_string(rng());
          return std::filesystem::temp_directory_path() /
                 std::filesystem::path("envy-install-test-" + suffix);
        }() },
        test_cache{ temp_root },
        eng{ test_cache, std::nullopt } {
    // Create temp cache directory
    std::filesystem::create_directories(temp_root);

    spec = recipe_spec::pool()->emplace("test.package@v1",
                                        recipe_spec::weak_ref{},
                                        "{}",
                                        std::nullopt,
                                        nullptr,
                                        nullptr,
                                        std::vector<recipe_spec *>{},
                                        std::nullopt,
                                        std::filesystem::path{});

    // Create Lua state first
    auto lua_state{ sol_util_make_lua_state() };
    lua_envy_install(*lua_state);

    // Initialize options to empty table
    r = std::unique_ptr<recipe>(new recipe{
        .key = recipe_key(*spec),
        .spec = spec,
        .exec_ctx = nullptr,
        .lua = std::move(lua_state),
        // lua_mutex is default-initialized
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
        .type = recipe_type::UNKNOWN,
        .cache_ptr = &test_cache,
        .default_shell_ptr = nullptr,
    });
  }

  ~install_test_fixture() {
    r.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_root, ec);
  }

  void set_check_verb(std::string_view check_code) {
    lua_pushstring(r->lua->lua_state(), std::string(check_code).c_str());
    lua_setglobal(r->lua->lua_state(), "check");
  }

  void set_install_function(std::string_view install_code) {
    std::string code = "install = " + std::string(install_code);
    if (luaL_dostring(r->lua->lua_state(), code.c_str()) != LUA_OK) {
      char const *err = lua_tostring(r->lua->lua_state(), -1);
      throw std::runtime_error(std::string("Failed to set install function: ") +
                               (err ? err : "unknown"));
    }
  }

  void acquire_lock() {
    auto result =
        test_cache.ensure_asset("test.package@v1", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    r->lock = std::move(result.lock);
  }

  void write_install_content() {
    REQUIRE(r->lock);
    auto install_file = r->lock->install_dir() / "output.txt";
    std::ofstream ofs{ install_file };
    ofs << "installed";
  }
};

}  // namespace

// ============================================================================
// Check XOR Cache validation tests
// ============================================================================

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase rejects user-managed package that calls mark_install_complete") {
  // Recipe with check verb (user-managed)
  set_check_verb("echo test");

  // Install function that incorrectly calls mark_install_complete
  set_install_function(R"(
    function(ctx)
      ctx.mark_install_complete()
    end
  )");

  acquire_lock();

  // run_install_phase should throw error about check XOR cache violation
  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("has check verb (user-managed)") != std::string::npos);
  CHECK(exception_msg.find("called mark_install_complete") != std::string::npos);
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase allows user-managed package without mark_install_complete") {
  // Recipe with check verb (user-managed)
  set_check_verb("echo test");

  // Install function that does NOT call mark_install_complete (correct)
  set_install_function(R"(
    function(ctx)
      -- User-managed packages do work but don't populate cache
    end
  )");

  acquire_lock();

  // Should not throw
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase allows cache-managed package with mark_install_complete") {
  // No check verb (cache-managed)
  lua_pushnil(r->lua->lua_state());
  lua_setglobal(r->lua->lua_state(), "check");

  // Install function that calls mark_install_complete (correct for cache-managed)
  set_install_function(R"(
    function(ctx)
      ctx.mark_install_complete()
    end
  )");

  acquire_lock();
  write_install_content();

  // Should not throw
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase allows cache-managed package without mark_install_complete "
    "(programmatic)") {
  // No check verb (cache-managed)
  lua_pushnil(r->lua->lua_state());
  lua_setglobal(r->lua->lua_state(), "check");

  // Install function that does NOT call mark_install_complete (programmatic package)
  set_install_function(R"(
    function(ctx)
      -- Programmatic packages don't populate cache either
    end
  )");

  acquire_lock();

  // Should not throw
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase rejects user-managed with string check that calls "
                  "mark_install_complete") {
  // String check verb (user-managed)
  lua_pushstring(r->lua->lua_state(), "echo test");
  lua_setglobal(r->lua->lua_state(), "check");

  // Install that incorrectly calls mark_install_complete
  set_install_function(R"(
    function(ctx)
      ctx.mark_install_complete()
    end
  )");

  acquire_lock();
  write_install_content();

  // Should throw - string check is still a check verb
  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("has check verb (user-managed)") != std::string::npos);
  CHECK(exception_msg.find("called mark_install_complete") != std::string::npos);
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase allows nil install with check verb (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  // Nil install - default behavior (promote stage to install)
  lua_pushnil(r->lua->lua_state());
  lua_setglobal(r->lua->lua_state(), "install");

  acquire_lock();

  // Should not throw - nil install doesn't call mark_install_complete
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase allows string install with check verb (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  // String install - shell script that doesn't call mark_install_complete
  // Note: shell installs auto-call mark_install_complete, so this SHOULD error
  lua_pushstring(r->lua->lua_state(), "echo 'installing'");
  lua_setglobal(r->lua->lua_state(), "install");

  acquire_lock();

  // String installs auto-mark complete, so this should throw
  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("has check verb (user-managed)") != std::string::npos);
  CHECK(exception_msg.find("called mark_install_complete") != std::string::npos);
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase with no lock skips work (cache hit)") {
  // Don't acquire lock (simulates cache hit)
  r->lock = nullptr;

  set_check_verb("echo test");
  set_install_function(R"(
    function(ctx)
      error("Should not run - no lock means cache hit")
    end
  )");

  // Should not throw or run install function
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

}  // namespace envy
