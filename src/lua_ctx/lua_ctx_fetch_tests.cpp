#include "lua_ctx_bindings.h"

#include "doctest.h"

#include "sol_util.h"

#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

// RAII helper for temporary test directories
struct temp_dir {
  fs::path path;

  temp_dir() : path(fs::temp_directory_path() / "envy_test_ctx_fetch") {
    fs::remove_all(path);
    fs::create_directories(path);
    fs::create_directories(path / "tmp");
    fs::create_directories(path / "fetch");
    fs::create_directories(path / "stage");
  }

  ~temp_dir() { fs::remove_all(path); }
};

}  // namespace

TEST_CASE("ctx.fetch - collision detection with same basename") {
  temp_dir tmp;

  // Create three test files with same basename
  fs::path file1{ tmp.path / "source1" / "file.txt" };
  fs::path file2{ tmp.path / "source2" / "file.txt" };
  fs::path file3{ tmp.path / "source3" / "file.txt" };

  fs::create_directories(file1.parent_path());
  fs::create_directories(file2.parent_path());
  fs::create_directories(file3.parent_path());

  std::ofstream(file1) << "content1";
  std::ofstream(file2) << "content2";
  std::ofstream(file3) << "content3";

  // Setup fetch_phase_ctx with collision tracking
  envy::fetch_phase_ctx ctx;
  ctx.fetch_dir = tmp.path / "fetch";
  ctx.run_dir = tmp.path / "tmp";
  ctx.stage_dir = tmp.path / "stage";
  ctx.used_basenames = {};  // Empty, will track collisions
  ctx.engine_ = nullptr;
  ctx.recipe_ = nullptr;

  // Setup Lua and register ctx.fetch
  auto lua{ envy::sol_util_make_lua_state() };
  sol::table ctx_table{ lua->create_table() };
  envy::lua_ctx_bindings_register_fetch_phase(ctx_table, &ctx);
  envy::lua_ctx_add_common_bindings(ctx_table, &ctx);
  (*lua)["ctx"] = ctx_table;

  // Fetch three files with same basename
  std::string lua_code =
      "return ctx.fetch({\"" + file1.generic_string() + "\", \"" + file2.generic_string() + "\", \"" +
      file3.generic_string() + "\"})";

  // Execute
  auto result{ lua->safe_script(lua_code, sol::script_pass_on_error) };
  REQUIRE(result.valid());

  sol::table files = result;
  std::string basename1 = files[1];
  std::string basename2 = files[2];
  std::string basename3 = files[3];

  // Verify collision suffixes were added
  CHECK(basename1 == "file.txt");
  CHECK(basename2 == "file-2.txt");
  CHECK(basename3 == "file-3.txt");

  // Verify files actually exist in tmp with renamed basenames
  CHECK(fs::exists(tmp.path / "tmp" / "file.txt"));
  CHECK(fs::exists(tmp.path / "tmp" / "file-2.txt"));
  CHECK(fs::exists(tmp.path / "tmp" / "file-3.txt"));

  // Verify collision tracking was updated
  CHECK(ctx.used_basenames.contains("file.txt"));
  CHECK(ctx.used_basenames.contains("file-2.txt"));
  CHECK(ctx.used_basenames.contains("file-3.txt"));
}

TEST_CASE("ctx.fetch - collision detection preserves extension") {
  temp_dir tmp;

  // Create files with different extensions but would collide
  fs::path file1{ tmp.path / "a" / "tool.tar.gz" };
  fs::path file2{ tmp.path / "b" / "tool.tar.gz" };
  fs::path file3{ tmp.path / "c" / "tool.tar.gz" };

  fs::create_directories(file1.parent_path());
  fs::create_directories(file2.parent_path());
  fs::create_directories(file3.parent_path());

  std::ofstream(file1) << "a";
  std::ofstream(file2) << "b";
  std::ofstream(file3) << "c";

  envy::fetch_phase_ctx ctx;
  ctx.fetch_dir = tmp.path / "fetch";
  ctx.run_dir = tmp.path / "tmp";
  ctx.stage_dir = tmp.path / "stage";
  ctx.used_basenames = {};
  ctx.engine_ = nullptr;
  ctx.recipe_ = nullptr;

  auto lua{ envy::sol_util_make_lua_state() };
  sol::table ctx_table{ lua->create_table() };
  envy::lua_ctx_bindings_register_fetch_phase(ctx_table, &ctx);
  envy::lua_ctx_add_common_bindings(ctx_table, &ctx);
  (*lua)["ctx"] = ctx_table;

  std::string lua_code =
      "return ctx.fetch({\"" + file1.generic_string() + "\", \"" + file2.generic_string() + "\", \"" +
      file3.generic_string() + "\"})";

  auto result{ lua->safe_script(lua_code, sol::script_pass_on_error) };
  REQUIRE(result.valid());

  sol::table files = result;
  std::string basename1 = files[1];
  std::string basename2 = files[2];
  std::string basename3 = files[3];

  // Verify extension is preserved (splits at last dot)
  CHECK(basename1 == "tool.tar.gz");
  CHECK(basename2 == "tool.tar-2.gz");
  CHECK(basename3 == "tool.tar-3.gz");
}

TEST_CASE("ctx.fetch - collision detection with no extension") {
  temp_dir tmp;

  // Create files with no extension
  fs::path file1{ tmp.path / "a" / "README" };
  fs::path file2{ tmp.path / "b" / "README" };

  fs::create_directories(file1.parent_path());
  fs::create_directories(file2.parent_path());

  std::ofstream(file1) << "readme1";
  std::ofstream(file2) << "readme2";

  envy::fetch_phase_ctx ctx;
  ctx.fetch_dir = tmp.path / "fetch";
  ctx.run_dir = tmp.path / "tmp";
  ctx.stage_dir = tmp.path / "stage";
  ctx.used_basenames = {};
  ctx.engine_ = nullptr;
  ctx.recipe_ = nullptr;

  auto lua{ envy::sol_util_make_lua_state() };
  sol::table ctx_table{ lua->create_table() };
  envy::lua_ctx_bindings_register_fetch_phase(ctx_table, &ctx);
  envy::lua_ctx_add_common_bindings(ctx_table, &ctx);
  (*lua)["ctx"] = ctx_table;

  std::string lua_code =
      "return ctx.fetch({\"" + file1.generic_string() + "\", \"" + file2.generic_string() + "\"})";

  auto result{ lua->safe_script(lua_code, sol::script_pass_on_error) };
  REQUIRE(result.valid());

  sol::table files = result;
  std::string basename1 = files[1];
  std::string basename2 = files[2];

  // Verify suffix added without extension
  CHECK(basename1 == "README");
  CHECK(basename2 == "README-2");
}

TEST_CASE("ctx.fetch - collision tracking across multiple calls") {
  temp_dir tmp;

  fs::path file1{ tmp.path / "a" / "lib.so" };
  fs::path file2{ tmp.path / "b" / "lib.so" };

  fs::create_directories(file1.parent_path());
  fs::create_directories(file2.parent_path());

  std::ofstream(file1) << "lib1";
  std::ofstream(file2) << "lib2";

  envy::fetch_phase_ctx ctx;
  ctx.fetch_dir = tmp.path / "fetch";
  ctx.run_dir = tmp.path / "tmp";
  ctx.stage_dir = tmp.path / "stage";
  ctx.used_basenames = {};
  ctx.engine_ = nullptr;
  ctx.recipe_ = nullptr;

  auto lua{ envy::sol_util_make_lua_state() };
  sol::table ctx_table{ lua->create_table() };
  envy::lua_ctx_bindings_register_fetch_phase(ctx_table, &ctx);
  envy::lua_ctx_add_common_bindings(ctx_table, &ctx);
  (*lua)["ctx"] = ctx_table;

  // First fetch call
  std::string lua_code1 = "local f1 = ctx.fetch(\"" + file1.generic_string() + "\"); return f1";
  auto result1{ lua->safe_script(lua_code1, sol::script_pass_on_error) };
  REQUIRE(result1.valid());
  std::string basename1{ result1.get<std::string>() };
  CHECK(basename1 == "lib.so");

  // Second fetch call - should detect collision from first call
  std::string lua_code2 = "local f2 = ctx.fetch(\"" + file2.generic_string() + "\"); return f2";
  auto result2{ lua->safe_script(lua_code2, sol::script_pass_on_error) };
  REQUIRE(result2.valid());
  std::string basename2{ result2.get<std::string>() };
  CHECK(basename2 == "lib-2.so");

  // Verify both tracked
  CHECK(ctx.used_basenames.size() == 2);
  CHECK(ctx.used_basenames.contains("lib.so"));
  CHECK(ctx.used_basenames.contains("lib-2.so"));
}
