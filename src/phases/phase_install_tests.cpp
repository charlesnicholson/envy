#include "phase_install.h"

#include "cache.h"
#include "engine.h"
#include "lua_envy.h"
#include "phase_check.h"
#include "pkg.h"
#include "pkg_cfg.h"
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
  pkg_cfg *cfg;
  std::unique_ptr<pkg> p;
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

    cfg = pkg_cfg::pool()->emplace("test.package@v1",
                                   pkg_cfg::weak_ref{},
                                   "{}",
                                   std::nullopt,
                                   nullptr,
                                   nullptr,
                                   std::vector<pkg_cfg *>{},
                                   std::nullopt,
                                   std::filesystem::path{});

    // Create Lua state first
    auto lua_state{ sol_util_make_lua_state() };
    lua_envy_install(*lua_state);

    // Initialize options to empty table
    p = std::unique_ptr<pkg>(new pkg{ .key = pkg_key(*cfg),
                                      .cfg = cfg,
                                      .cache_ptr = &test_cache,
                                      .default_shell_ptr = nullptr,
                                      .exec_ctx = nullptr,
                                      .lua = std::move(lua_state),
                                      // lua_mutex is default-initialized
                                      .lock = nullptr,
                                      .canonical_identity_hash = {},
                                      .pkg_path = std::filesystem::path{},
                                      .spec_file_path = std::nullopt,
                                      .result_hash = {},
                                      .type = pkg_type::UNKNOWN,
                                      .declared_dependencies = {},
                                      .owned_dependency_cfgs = {},
                                      .dependencies = {},
                                      .product_dependencies = {},
                                      .weak_references = {},
                                      .products = {},
                                      .resolved_weak_dependency_keys = {} });
  }

  ~install_test_fixture() {
    p.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_root, ec);
  }

  sol::state_view lua_state() { return sol::state_view{ *p->lua }; }

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
    auto result = test_cache.ensure_pkg("test.package@v1", "darwin", "arm64", "deadbeef");
    REQUIRE(result.lock != nullptr);
    p->lock = std::move(result.lock);
  }

  void write_install_content() {
    REQUIRE(p->lock);
    auto install_file = p->lock->install_dir() / "output.txt";
    std::ofstream ofs{ install_file };
    ofs << "installed";
  }
};

}  // namespace

// ============================================================================
// Check XOR Cache validation tests
// ============================================================================

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase provides nil install_dir for user-managed packages") {
  // Spec with check verb (user-managed)
  set_check_verb("echo test");

  // Install function that verifies install_dir is nil for user-managed packages
  // and properly guards against using it as a path
  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      -- install_dir is nil for user-managed packages
      if install_dir ~= nil then
        error("expected install_dir to be nil for user-managed package")
      end
      -- Verify other directories are valid strings
      if type(stage_dir) ~= "string" then
        error("expected stage_dir to be string")
      end
    end
  )");

  acquire_lock();

  // Should succeed - the function properly checks for nil
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase allows user-managed package without mark_install_complete") {
  // Spec with check verb (user-managed)
  set_check_verb("echo test");

  // Install function with new signature (install_dir is nil for user-managed)
  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      -- User-managed packages receive nil for install_dir
      -- They do work but don't populate cache
    end
  )");

  acquire_lock();

  // Should not throw
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase auto-marks cache-managed package complete on success") {
  // No check verb (cache-managed)
  clear_check_verb();

  // Install function with new signature (auto-marks complete)
  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      -- Auto-marked complete on successful return
    end
  )");

  acquire_lock();
  write_install_content();

  // Should not throw and should mark complete
  CHECK_NOTHROW(run_install_phase(p.get(), eng));

  // Verify pkg was marked complete
  CHECK(p->pkg_path.string().find("pkg") != std::string::npos);
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install phase allows cache-managed package without mark_install_complete "
    "(programmatic)") {
  // No check verb (cache-managed)
  clear_check_verb();

  // Install function with new signature
  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      -- Programmatic packages don't populate cache either
    end
  )");

  acquire_lock();

  // Should not throw
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase user-managed receives nil install_dir") {
  // String check verb (user-managed)
  set_check_verb("echo test");

  // Install function that verifies install_dir is nil for user-managed
  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      if install_dir ~= nil then
        error("expected install_dir to be nil for user-managed package")
      end
      -- stage_dir, fetch_dir, tmp_dir should still be valid strings
      if type(stage_dir) ~= "string" then
        error("expected stage_dir to be string")
      end
      if type(fetch_dir) ~= "string" then
        error("expected fetch_dir to be string")
      end
      if type(tmp_dir) ~= "string" then
        error("expected tmp_dir to be string")
      end
    end
  )");

  acquire_lock();
  write_install_content();

  // Should not throw - all assertions pass
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install phase allows nil install with check verb (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  // Nil install - default behavior (promote stage to install)
  clear_install_verb();

  acquire_lock();

  // Should not throw - nil install doesn't call mark_install_complete
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
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
  CHECK_NOTHROW(run_install_phase(p.get(), eng));

  // Verify asset_path was NOT set (not marked complete for user-managed)
  CHECK(p->pkg_path.empty());
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
  CHECK_NOTHROW(run_install_phase(p.get(), eng));

  // Verify asset_path was set (marked complete for cache-managed)
  CHECK(!p->pkg_path.empty());
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
    run_install_phase(p.get(), eng);
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
    run_install_phase(p.get(), eng);
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
  p->lock = nullptr;

  set_check_verb("echo test");
  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      error("Should not run - no lock means cache hit")
    end
  )");

  // Should not throw or run install function
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

// ============================================================================
// Install function return type validation tests
// ============================================================================

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning nil succeeds (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return nil
    end
  )");

  acquire_lock();
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning nil succeeds (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return nil
    end
  )");

  acquire_lock();
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function with no return succeeds (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      -- No return statement
    end
  )");

  acquire_lock();
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function returning number throws with type error") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return 42
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(p.get(), eng);
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
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return {foo = "bar"}
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(p.get(), eng);
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
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return true
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(p.get(), eng);
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
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return "echo 'returned string executed'"
    end
  )");

  acquire_lock();

  // Should execute returned string and mark complete
  CHECK_NOTHROW(run_install_phase(p.get(), eng));

  // Verify asset_path was set (indicates marked complete)
  CHECK(!p->pkg_path.empty());
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install function returning string does not mark complete (user-managed)") {
  // Check verb (user-managed)
  set_check_verb("echo test");

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return "exit 0"
    end
  )");

  acquire_lock();

  // Should not throw - user-managed packages can return strings
  run_install_phase(p.get(), eng);

  // Verify asset_path was NOT set (user-managed packages don't mark complete)
  CHECK(p->pkg_path.empty());
}

TEST_CASE_FIXTURE(install_test_fixture,
                  "install function can call envy.run and return string (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      envy.run("echo 'first command'")
      return "echo 'second command'"
    end
  )");

  acquire_lock();

  // Both envy.run() and returned string should execute
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
  CHECK(!p->pkg_path.empty());
}

TEST_CASE_FIXTURE(
    install_test_fixture,
    "install function returning string with non-zero exit throws (cache-managed)") {
  // No check verb (cache-managed)
  clear_check_verb();

  set_install_function(R"(
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return "exit 1"
    end
  )");

  acquire_lock();

  bool exception_thrown = false;
  std::string exception_msg;
  try {
    run_install_phase(p.get(), eng);
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
    function(install_dir, stage_dir, fetch_dir, tmp_dir)
      return ""
    end
  )");

  acquire_lock();

  // Empty string should succeed (no-op shell script)
  CHECK_NOTHROW(run_install_phase(p.get(), eng));
  CHECK(!p->pkg_path.empty());
}

}  // namespace envy
