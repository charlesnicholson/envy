#include "luarc.h"

#include "doctest.h"
#include "picojson.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("make_portable_path") {
#ifdef _WIN32
  char const *home{ std::getenv("USERPROFILE") };
  char const env_var[]{ "${env:USERPROFILE}" };
  char const sep{ '\\' };
#else
  char const *home{ std::getenv("HOME") };
  char const env_var[]{ "${env:HOME}" };
  char const sep{ '/' };
#endif
  REQUIRE(home);
  std::string const home_str{ home };

  SUBCASE("replaces home prefix with env var") {
    fs::path const path{ home_str + sep + "Library" + sep + "Caches" };
    CHECK(envy::make_portable_path(path) ==
          std::string{ env_var } + "/" + "Library" + "/" + "Caches");
  }

  SUBCASE("preserves paths not under home") {
#ifdef _WIN32
    fs::path const path{ "C:\\Windows\\System32" };
    CHECK(envy::make_portable_path(path) == "C:/Windows/System32");
#else
    fs::path const path{ "/tmp/some/other/path" };
    CHECK(envy::make_portable_path(path) == "/tmp/some/other/path");
#endif
  }

  SUBCASE("handles home as exact path") {
    fs::path const path{ home_str };
    CHECK(envy::make_portable_path(path) == env_var);
  }

  SUBCASE("does not replace partial home matches") {
    fs::path const path{ home_str + "-other" + sep + "something" };
    std::string expected{ home_str + "-other" + sep + "something" };
    std::replace(expected.begin(), expected.end(), '\\', '/');
    CHECK(envy::make_portable_path(path) == expected);
  }
}

namespace {

// Parse rewrite result and extract workspace.library array entries
std::vector<std::string> parse_library(std::string const &json) {
  picojson::value root;
  picojson::parse(root, json);
  auto &lib{ root.get<picojson::object>().at("workspace.library").get<picojson::array>() };
  std::vector<std::string> result;
  for (auto const &v : lib) { result.push_back(v.get<std::string>()); }
  return result;
}

picojson::array parse_library_raw(std::string const &json) {
  picojson::value root;
  picojson::parse(root, json);
  return root.get<picojson::object>().at("workspace.library").get<picojson::array>();
}

}  // namespace

TEST_CASE("rewrite_luarc_types_path") {
  std::string const current_path{ "${env:HOME}/.cache/envy/envy/0.1.0" };
  std::string const new_path{ "${env:HOME}/.cache/envy/envy/0.2.0" };

  SUBCASE("updates old version path to new") {
    std::string const input{ R"({"workspace.library": [")" + current_path + R"("]})" };
    auto result{ envy::rewrite_luarc_types_path(input, new_path) };
    REQUIRE(result.has_value());
    auto lib{ parse_library(*result) };
    REQUIRE(lib.size() == 1);
    CHECK(lib[0] == new_path);
  }

  SUBCASE("already-current path returns nullopt") {
    std::string const input{ R"({"workspace.library": [")" + current_path + R"("]})" };
    auto result{ envy::rewrite_luarc_types_path(input, current_path) };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("invalid JSON returns nullopt") {
    auto result{ envy::rewrite_luarc_types_path("{not valid json", new_path) };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("empty string returns nullopt") {
    auto result{ envy::rewrite_luarc_types_path("", new_path) };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("root not object returns nullopt") {
    auto result{ envy::rewrite_luarc_types_path("[1, 2, 3]", new_path) };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("missing workspace.library returns nullopt") {
    auto result{ envy::rewrite_luarc_types_path(R"({"other.key": 42})", new_path) };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("workspace.library not array returns nullopt") {
    auto result{ envy::rewrite_luarc_types_path(R"({"workspace.library": "not-an-array"})",
                                                new_path) };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("expected path with no slash returns nullopt") {
    auto result{ envy::rewrite_luarc_types_path(R"({"workspace.library": ["something"]})",
                                                "no-slash") };
    CHECK_FALSE(result.has_value());
  }

  SUBCASE("no envy entry — adds entry to end of library") {
    auto result{ envy::rewrite_luarc_types_path(
        R"({"workspace.library": ["/some/other/lib"]})",
        new_path) };
    REQUIRE(result.has_value());
    auto lib{ parse_library(*result) };
    REQUIRE(lib.size() == 2);
    CHECK(lib[0] == "/some/other/lib");
    CHECK(lib[1] == new_path);
  }

  SUBCASE("empty library array — adds envy entry") {
    auto result{ envy::rewrite_luarc_types_path(R"({"workspace.library": []})",
                                                new_path) };
    REQUIRE(result.has_value());
    auto lib{ parse_library(*result) };
    REQUIRE(lib.size() == 1);
    CHECK(lib[0] == new_path);
  }

  SUBCASE("library with only non-string entries — adds envy entry") {
    auto result{ envy::rewrite_luarc_types_path(R"({"workspace.library": [42, true]})",
                                                new_path) };
    REQUIRE(result.has_value());
    auto lib{ parse_library_raw(*result) };
    REQUIRE(lib.size() == 3);
    CHECK(lib[0].is<double>());
    CHECK(lib[1].is<bool>());
    CHECK(lib[2].get<std::string>() == new_path);
  }

  SUBCASE("multiple library entries — only envy entry updated, others preserved") {
    std::string const input{ R"({"workspace.library": ["/usr/local/lua-libs", ")" +
                             current_path + R"(", "/another/lib"]})" };
    auto result{ envy::rewrite_luarc_types_path(input, new_path) };
    REQUIRE(result.has_value());
    auto lib{ parse_library(*result) };
    REQUIRE(lib.size() == 3);
    CHECK(lib[0] == "/usr/local/lua-libs");
    CHECK(lib[1] == new_path);
    CHECK(lib[2] == "/another/lib");
  }

  SUBCASE("other JSON keys are preserved") {
    std::string const input{
      R"({"diagnostics.globals": ["envy"], "workspace.library": [")" + current_path +
      R"("], "completion.enable": true})"
    };
    auto result{ envy::rewrite_luarc_types_path(input, new_path) };
    REQUIRE(result.has_value());
    picojson::value root;
    picojson::parse(root, *result);
    auto &obj{ root.get<picojson::object>() };
    CHECK(obj.count("diagnostics.globals") == 1);
    CHECK(obj.count("completion.enable") == 1);
  }

  SUBCASE("other JSON keys preserved when adding missing entry") {
    auto result{ envy::rewrite_luarc_types_path(
        R"({"diagnostics.globals": ["envy"], "workspace.library": [], "completion.enable": true})",
        new_path) };
    REQUIRE(result.has_value());
    picojson::value root;
    picojson::parse(root, *result);
    auto &obj{ root.get<picojson::object>() };
    CHECK(obj.count("diagnostics.globals") == 1);
    CHECK(obj.count("completion.enable") == 1);
    auto lib{ parse_library(*result) };
    REQUIRE(lib.size() == 1);
    CHECK(lib[0] == new_path);
  }

  SUBCASE("multiple custom entries preserved when adding missing envy entry") {
    auto result{ envy::rewrite_luarc_types_path(
        R"({"workspace.library": ["/custom/lib1", "/custom/lib2", "/custom/lib3"]})",
        new_path) };
    REQUIRE(result.has_value());
    auto lib{ parse_library(*result) };
    REQUIRE(lib.size() == 4);
    CHECK(lib[0] == "/custom/lib1");
    CHECK(lib[1] == "/custom/lib2");
    CHECK(lib[2] == "/custom/lib3");
    CHECK(lib[3] == new_path);
  }
}
