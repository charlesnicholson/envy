#include "phase_install.h"

#include "cache.h"
#include "engine.h"
#include "lua_envy.h"
#include "phase_check.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "sol_util.h"

#include "doctest.h"

#include <sol/sol.hpp>

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

  sol::state_view lua_state() { return sol::state_view{ *r->lua }; }

  void clear_check_verb() { lua_state()["CHECK"] = sol::lua_nil; }

  void set_check_verb(std::string_view check_code) {
    lua_state()["CHECK"] = std::string(check_code);
  }

  void clear_install_verb() { lua_state()["INSTALL"] = sol::lua_nil; }

  void set_install_string(std::string_view install_code) {
    lua_state()["INSTALL"] = std::string(install_code);
  }

  void set_install_function(std::string_view install_code) {
    auto state{ lua_state() };
    std::string code = "INSTALL = " + std::string(install_code);
    sol::protected_function_result result =
        state.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
      sol::error err = result;
      throw std::runtime_error(std::string("Failed to set install function: ") +
                               err.what());
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
  INFO("Exception message: ", exception_msg);
  // mark_install_complete is not exposed at all for user-managed packages,
  // so Lua sees it as nil and throws "attempt to call a nil value"
  CHECK(exception_msg.find("attempt to call a nil value") != std::string::npos);
  CHECK(exception_msg.find("mark_install_complete") != std::string::npos);
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
    "install phase auto-marks cache-managed package complete on success") {
  // No check verb (cache-managed)
  clear_check_verb();

  // Install function that returns successfully (auto-marks complete)
  set_install_function(R"(
    function(ctx)
      -- Auto-marked complete on successful return
    end
  )");

  acquire_lock();
  write_install_content();

  // Should not throw and should mark complete
  CHECK_NOTHROW(run_install_phase(r.get(), eng));

  // Verify asset was marked complete
  CHECK(r->asset_path.string().find("asset") != std::string::npos);
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase allows cache-managed package without mark_install_complete "
    "(programmatic)") {
  // No check verb (cache-managed)
  clear_check_verb();

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
                  "install phase rejects user-managed calling forbidden extract_all") {
  // String check verb (user-managed)
  set_check_verb("echo test");

  // Install that incorrectly calls extract_all (forbidden for user-managed)
  set_install_function(R"(
    function(ctx)
      ctx.extract_all()
    end
  )");

  acquire_lock();
  write_install_content();

  // Should throw - extract_all is forbidden for user-managed packages
  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  INFO("Exception message: ", exception_msg);
  // Forbidden APIs throw error mentioning "not available for user-managed"
  CHECK(exception_msg.find("not available for user-managed") != std::string::npos);
  CHECK(exception_msg.find("extract_all") != std::string::npos);
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase allows nil install with check verb (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  // Nil install - default behavior (promote stage to install)
  clear_install_verb();

  acquire_lock();

  // Should not throw - nil install doesn't call mark_install_complete
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase string install succeeds for user-managed without marking complete") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  // String install - runs command but doesn't mark complete for user-managed
  set_install_string("echo 'installing'");

  acquire_lock();

  // String installs should succeed for user-managed, but not mark complete
  CHECK_NOTHROW(run_install_phase(r.get(), eng));

  // Verify asset_path was NOT set (not marked complete for user-managed)
  CHECK(r->asset_path.empty());
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase string install succeeds for cache-managed and marks complete") {
  // No check verb (cache-managed)
  clear_check_verb();

  // String install
  set_install_string("echo 'installing'");

  acquire_lock();

  // String installs should succeed and mark complete for cache-managed
  CHECK_NOTHROW(run_install_phase(r.get(), eng));

  // Verify asset_path was set (marked complete for cache-managed)
  CHECK(!r->asset_path.empty());
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase string install with non-zero exit throws (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  // String install that fails
  set_install_string("exit 1");

  acquire_lock();

  // Should throw on non-zero exit regardless of user-managed status
  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("Install shell script failed") != std::string::npos);
  CHECK(exception_msg.find("exit code 1") != std::string::npos);
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase string install with non-zero exit throws (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  // String install that fails
  set_install_string("exit 1");

  acquire_lock();

  // Should throw on non-zero exit
  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("Install shell script failed") != std::string::npos);
  CHECK(exception_msg.find("exit code 1") != std::string::npos);
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

// ============================================================================
// Install function return type validation tests
// ============================================================================

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning nil succeeds (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return nil
    end
  )");

  acquire_lock();
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning nil succeeds (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  set_install_function(R"(
    function(ctx)
      return nil
    end
  )");

  acquire_lock();
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function with no return succeeds (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      -- No return statement
    end
  )");

  acquire_lock();
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning number throws with type error") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return 42
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("must return nil or string") != std::string::npos);
  CHECK(exception_msg.find("number") != std::string::npos);
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning table throws with type error") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return {foo = "bar"}
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("must return nil or string") != std::string::npos);
  CHECK(exception_msg.find("table") != std::string::npos);
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning boolean throws with type error") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return true
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("must return nil or string") != std::string::npos);
  CHECK(exception_msg.find("boolean") != std::string::npos);
}

// ============================================================================
// Install function returning string tests
// ============================================================================

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning string executes shell and marks complete "
                  "(cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return "echo 'returned string executed'"
    end
  )");

  acquire_lock();

  // Should execute returned string and mark complete
  CHECK_NOTHROW(run_install_phase(r.get(), eng));

  // Verify asset_path was set (indicates marked complete)
  CHECK(!r->asset_path.empty());
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install function returning string does not mark complete (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  set_install_function(R"(
    function(ctx)
      return "exit 0"
    end
  )");

  acquire_lock();

  // Should not throw - user-managed packages can return strings
  run_install_phase(r.get(), eng);

  // Verify asset_path was NOT set (user-managed packages don't mark complete)
  CHECK(r->asset_path.empty());
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function can call ctx.run and return string (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      ctx.run("echo 'first command'")
      return "echo 'second command'"
    end
  )");

  acquire_lock();

  // Both ctx.run() and returned string should execute
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
  CHECK(!r->asset_path.empty());
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install function returning string with non-zero exit throws (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return "exit 1"
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(r.get(), eng);
  } catch (std::runtime_error const &e) {
    exception_thrown = true;
    exception_msg = e.what();
  }
  REQUIRE(exception_thrown);
  CHECK(exception_msg.find("Install shell script failed") != std::string::npos);
  CHECK(exception_msg.find("exit code 1") != std::string::npos);
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning empty string succeeds (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(ctx)
      return ""
    end
  )");

  acquire_lock();

  // Empty string should succeed (no-op shell script)
  CHECK_NOTHROW(run_install_phase(r.get(), eng));
  CHECK(!r->asset_path.empty());
}

}  // namespace envy
