#include "lua_error_formatter.h"

#include "engine.h"
#include "lua_envy.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "sol_util.h"

#include "doctest.h"

#include <memory>

namespace envy {

// Extern declarations for testing internal functions (not in public API)
extern std::optional<int> extract_line_number(std::string const &error_msg);
extern std::vector<recipe_spec const *> build_provenance_chain(recipe_spec const *spec);

namespace {

// Helper fixture for creating test recipes
struct formatter_test_fixture {
  recipe_spec *spec;
  std::unique_ptr<recipe> r;

  formatter_test_fixture(std::string identity = "test.package@v1",
                         std::string options = "{}",
                         std::filesystem::path declaring_path = {},
                         recipe_spec *parent_spec = nullptr) {
    spec = recipe_spec::pool()->emplace(std::move(identity),
                                        recipe_spec::weak_ref{},
                                        std::move(options),
                                        std::nullopt,
                                        parent_spec,
                                        nullptr,
                                        std::vector<recipe_spec *>{},
                                        std::nullopt,
                                        std::move(declaring_path));

    auto lua_state = sol_util_make_lua_state();
    lua_envy_install(*lua_state);

    r = std::unique_ptr<recipe>(new recipe{
        .key = recipe_key(*spec),
        .spec = spec,
        .exec_ctx = nullptr,
        .lua = std::move(lua_state),
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
        .cache_ptr = nullptr,
        .default_shell_ptr = nullptr,
    });
  }
};

}  // namespace

// ============================================================================
// extract_line_number() tests
// ============================================================================

TEST_CASE("extract_line_number extracts line from standard Lua error") {
  std::string error_msg = "/path/to/recipe.lua:42: assertion failed";
  auto line_num = extract_line_number(error_msg);
  REQUIRE(line_num.has_value());
  CHECK(*line_num == 42);
}

TEST_CASE("extract_line_number handles multi-digit line numbers") {
  std::string error_msg = "recipe.lua:1234: some error";
  auto line_num = extract_line_number(error_msg);
  REQUIRE(line_num.has_value());
  CHECK(*line_num == 1234);
}

TEST_CASE("extract_line_number returns nullopt when no .lua: pattern") {
  std::string error_msg = "generic error message";
  auto line_num = extract_line_number(error_msg);
  CHECK_FALSE(line_num.has_value());
}

TEST_CASE("extract_line_number returns nullopt when no colon after line number") {
  std::string error_msg = "recipe.lua:42";
  auto line_num = extract_line_number(error_msg);
  CHECK_FALSE(line_num.has_value());
}

TEST_CASE("extract_line_number returns nullopt for non-numeric line number") {
  std::string error_msg = "recipe.lua:abc: error";
  auto line_num = extract_line_number(error_msg);
  CHECK_FALSE(line_num.has_value());
}

TEST_CASE("extract_line_number handles line number 1") {
  std::string error_msg = "recipe.lua:1: error at top of file";
  auto line_num = extract_line_number(error_msg);
  REQUIRE(line_num.has_value());
  CHECK(*line_num == 1);
}

// ============================================================================
// build_provenance_chain() tests
// ============================================================================

TEST_CASE("build_provenance_chain returns single element for recipe without parent") {
  formatter_test_fixture f;
  auto chain = build_provenance_chain(f.spec);
  REQUIRE(chain.size() == 1);
  CHECK(chain[0] == f.spec);
}

TEST_CASE("build_provenance_chain builds chain with parent") {
  formatter_test_fixture parent{ "parent.package@v1" };
  formatter_test_fixture child{ "child.package@v1", "{}", {}, parent.spec };

  auto chain = build_provenance_chain(child.spec);
  REQUIRE(chain.size() == 2);
  CHECK(chain[0] == child.spec);
  CHECK(chain[1] == parent.spec);
}

TEST_CASE("build_provenance_chain builds chain with grandparent") {
  formatter_test_fixture grandparent{ "grandparent.package@v1" };
  formatter_test_fixture parent{ "parent.package@v1", "{}", {}, grandparent.spec };
  formatter_test_fixture child{ "child.package@v1", "{}", {}, parent.spec };

  auto chain = build_provenance_chain(child.spec);
  REQUIRE(chain.size() == 3);
  CHECK(chain[0] == child.spec);
  CHECK(chain[1] == parent.spec);
  CHECK(chain[2] == grandparent.spec);
}

TEST_CASE("build_provenance_chain handles nullptr") {
  auto chain = build_provenance_chain(nullptr);
  CHECK(chain.empty());
}

// ============================================================================
// format_lua_error() tests
// ============================================================================

TEST_CASE("format_lua_error includes identity in header") {
  formatter_test_fixture f{ "my.package@v1.2.3" };

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Lua error in my.package@v1.2.3") != std::string::npos);
}

TEST_CASE("format_lua_error includes error message") {
  formatter_test_fixture f;

  lua_error_context ctx{ .lua_error_message = "assertion failed: version required",
                         .r = f.r.get(),
                         .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("assertion failed: version required") != std::string::npos);
}

TEST_CASE("format_lua_error includes recipe_file_path when present") {
  formatter_test_fixture f;
  f.r->recipe_file_path = std::filesystem::path("/home/user/.envy/recipes/test.lua");

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Recipe file: /home/user/.envy/recipes/test.lua") !=
        std::string::npos);
}

TEST_CASE("format_lua_error includes line number when extractable") {
  formatter_test_fixture f;
  f.r->recipe_file_path = std::filesystem::path("/path/to/recipe.lua");

  lua_error_context ctx{ .lua_error_message = "recipe.lua:42: assertion failed",
                         .r = f.r.get(),
                         .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Recipe file: /path/to/recipe.lua:42") != std::string::npos);
}

TEST_CASE("format_lua_error omits recipe_file_path when not present") {
  formatter_test_fixture f;
  // recipe_file_path not set

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Recipe file:") == std::string::npos);
}

TEST_CASE("format_lua_error includes declaring_file_path") {
  formatter_test_fixture f{ "test.package@v1",
                            "{}",
                            std::filesystem::path("/path/to/manifest.lua") };

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Declared in: /path/to/manifest.lua") != std::string::npos);
}

TEST_CASE("format_lua_error includes phase when provided") {
  formatter_test_fixture f;

  lua_error_context ctx{ .lua_error_message = "test error",
                         .r = f.r.get(),
                         .phase = "build" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Phase: build") != std::string::npos);
}

TEST_CASE("format_lua_error omits phase when empty") {
  formatter_test_fixture f;

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Phase:") == std::string::npos);
}

TEST_CASE("format_lua_error includes serialized options") {
  formatter_test_fixture f{ "test.package@v1", R"({"version":"3.13.9"})" };

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find(R"(Options: {"version":"3.13.9"})") != std::string::npos);
}

TEST_CASE("format_lua_error includes options in header when non-empty") {
  formatter_test_fixture f{ "test.package@v1", R"({"version":"3.13.9"})" };

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find(R"(Lua error in test.package@v1{"version":"3.13.9"})") !=
        std::string::npos);
}

TEST_CASE("format_lua_error omits provenance chain for single recipe") {
  formatter_test_fixture f;

  lua_error_context ctx{ .lua_error_message = "test error", .r = f.r.get(), .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Provenance chain:") == std::string::npos);
}

TEST_CASE("format_lua_error includes provenance chain for nested dependencies") {
  formatter_test_fixture parent{ "parent.package@v1",
                                 "{}",
                                 std::filesystem::path("manifest.lua") };
  formatter_test_fixture child{ "child.package@v1",
                                "{}",
                                std::filesystem::path("parent.lua"),
                                parent.spec };

  lua_error_context ctx{ .lua_error_message = "test error",
                         .r = child.r.get(),
                         .phase = "" };

  std::string result = format_lua_error(ctx);
  CHECK(result.find("Provenance chain:") != std::string::npos);
  CHECK(result.find("child.package@v1") != std::string::npos);
  CHECK(result.find("parent.package@v1") != std::string::npos);
  CHECK(result.find("parent.lua") != std::string::npos);
  CHECK(result.find("manifest.lua") != std::string::npos);
}

TEST_CASE("format_lua_error full example with all context") {
  formatter_test_fixture parent{ "test.python@r3.13",
                                 "{}",
                                 std::filesystem::path("/home/user/manifest.lua") };
  formatter_test_fixture child{ "test.ninja@r1.11.1",
                                R"({"version":"1.11.1"})",
                                std::filesystem::path(
                                    "/home/user/.envy/recipes/python.lua"),
                                parent.spec };

  child.r->recipe_file_path = std::filesystem::path("/home/user/.envy/recipes/ninja.lua");

  lua_error_context ctx{ .lua_error_message =
                             "ninja.lua:42: assertion failed: version mismatch",
                         .r = child.r.get(),
                         .phase = "build" };

  std::string result = format_lua_error(ctx);

  // Verify all components present
  CHECK(result.find("Lua error in test.ninja@r1.11.1") != std::string::npos);
  CHECK(result.find(R"({"version":"1.11.1"})") != std::string::npos);
  CHECK(result.find("assertion failed: version mismatch") != std::string::npos);
  CHECK(result.find("Recipe file: /home/user/.envy/recipes/ninja.lua:42") !=
        std::string::npos);
  CHECK(result.find("Declared in: /home/user/.envy/recipes/python.lua") !=
        std::string::npos);
  CHECK(result.find("Phase: build") != std::string::npos);
  CHECK(result.find("Provenance chain:") != std::string::npos);
  CHECK(result.find("test.ninja@r1.11.1") != std::string::npos);
  CHECK(result.find("test.python@r3.13") != std::string::npos);
}

}  // namespace envy
