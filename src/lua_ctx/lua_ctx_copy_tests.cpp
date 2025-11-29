#include "lua_ctx_bindings.h"
#include "sol_util.h"

#include "doctest.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

using envy::sol_state_ptr;

// RAII helper for temporary test directories
struct temp_dir {
  fs::path path;

  temp_dir() : path(fs::temp_directory_path() / "envy_test_ctx_copy") {
    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~temp_dir() { fs::remove_all(path); }
};

// Helper to create a Lua state with ctx.copy registered
struct lua_ctx_copy_fixture {
  sol_state_ptr lua;
  temp_dir tmp;
  envy::lua_ctx_common ctx;

  lua_ctx_copy_fixture() {
    ctx.fetch_dir = tmp.path;
    ctx.run_dir = tmp.path;
    ctx.engine_ = nullptr;
    ctx.recipe_ = nullptr;

    lua = envy::sol_util_make_lua_state();
    (*lua)["copy_fn"] = envy::make_ctx_copy(&ctx);
  }

  void create_file(fs::path const &rel_path, std::string const &content) {
    fs::path full{ tmp.path / rel_path };
    if (full.has_parent_path()) { fs::create_directories(full.parent_path()); }
    std::ofstream(full) << content;
  }

  bool file_exists(fs::path const &rel_path) {
    return fs::exists(tmp.path / rel_path) && fs::is_regular_file(tmp.path / rel_path);
  }

  bool dir_exists(fs::path const &rel_path) {
    return fs::exists(tmp.path / rel_path) && fs::is_directory(tmp.path / rel_path);
  }

  std::string read_file(fs::path const &rel_path) {
    std::ifstream ifs(tmp.path / rel_path);
    return std::string((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
  }
};

}  // namespace

TEST_CASE("ctx.copy - file to file") {
  lua_ctx_copy_fixture fixture;
  fixture.create_file("src.txt", "test content");

  auto result{ fixture.lua->safe_script("copy_fn('src.txt', 'dst.txt')") };
  CHECK(result.valid());
  CHECK(fixture.file_exists("dst.txt"));
  CHECK(fixture.read_file("dst.txt") == "test content");
}

TEST_CASE("ctx.copy - file to existing directory") {
  lua_ctx_copy_fixture fixture;
  fixture.create_file("src.txt", "test content");
  fs::create_directories(fixture.tmp.path / "dest_dir");

  auto result{ fixture.lua->safe_script("copy_fn('src.txt', 'dest_dir')") };
  CHECK(result.valid());
  CHECK(fixture.file_exists("dest_dir/src.txt"));
  CHECK(fixture.read_file("dest_dir/src.txt") == "test content");
}

TEST_CASE("ctx.copy - file to new directory path") {
  lua_ctx_copy_fixture fixture;
  fixture.create_file("src.txt", "test content");

  auto result{ fixture.lua->safe_script("copy_fn('src.txt', 'subdir/dst.txt')") };
  CHECK(result.valid());
  CHECK(fixture.file_exists("subdir/dst.txt"));
  CHECK(fixture.read_file("subdir/dst.txt") == "test content");
}

TEST_CASE("ctx.copy - directory to directory (recursive)") {
  lua_ctx_copy_fixture fixture;
  fixture.create_file("srcdir/file1.txt", "content1");
  fixture.create_file("srcdir/file2.txt", "content2");
  fixture.create_file("srcdir/sub/file3.txt", "content3");

  auto result{ fixture.lua->safe_script("copy_fn('srcdir', 'dstdir')") };
  CHECK(result.valid());
  CHECK(fixture.file_exists("dstdir/file1.txt"));
  CHECK(fixture.file_exists("dstdir/file2.txt"));
  CHECK(fixture.file_exists("dstdir/sub/file3.txt"));
}

TEST_CASE("ctx.copy - overwrite existing file") {
  lua_ctx_copy_fixture fixture;
  fixture.create_file("src.txt", "new content");
  fixture.create_file("dst.txt", "old content");

  auto result{ fixture.lua->safe_script("copy_fn('src.txt', 'dst.txt')") };
  CHECK(result.valid());
  CHECK(fixture.read_file("dst.txt") == "new content");
}

TEST_CASE("ctx.copy - missing source file") {
  lua_ctx_copy_fixture fixture;

  auto result{ fixture.lua->script("copy_fn('missing.txt', 'dst.txt')", sol::script_pass_on_error) };
  CHECK(!result.valid());  // Should error
}

TEST_CASE("ctx.copy - relative paths resolved against run_dir") {
  lua_ctx_copy_fixture fixture;
  fixture.create_file("src.txt", "test content");

  auto result{ fixture.lua->safe_script("copy_fn('./src.txt', './dst.txt')") };
  CHECK(result.valid());
  CHECK(fixture.file_exists("dst.txt"));
}

TEST_CASE("ctx.copy - absolute paths") {
  lua_ctx_copy_fixture fixture;
  fs::path abs_src{ fixture.tmp.path / "src.txt" };
  fs::path abs_dst{ fixture.tmp.path / "dst.txt" };
  fixture.create_file("src.txt", "test content");

  std::string lua_code{ "copy_fn('" + abs_src.string() + "', '" + abs_dst.string() + "')" };
  auto result{ fixture.lua->safe_script(lua_code) };
  CHECK(result.valid());
  CHECK(fixture.file_exists("dst.txt"));
}
