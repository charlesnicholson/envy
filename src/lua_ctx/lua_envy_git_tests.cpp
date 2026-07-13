#include "lua_envy_git.h"

#include "sol_util.h"

#include "doctest.h"

#include <string>
#include <vector>

namespace {

using envy::git_ref_entry;
using envy::git_ref_is_full_sha;
using envy::git_resolve_ref;

// 40-char lowercase-hex oids (a/b/c/d/e are hex digits).
std::string const kOidA(40, 'a');
std::string const kOidB(40, 'b');
std::string const kOidC(40, 'c');
std::string const kOidD(40, 'd');

}  // namespace

// ---------------------------------------------------------------------------
// git_ref_is_full_sha
// ---------------------------------------------------------------------------

TEST_CASE("git_ref_is_full_sha accepts 40-char sha1 hex") {
  CHECK(git_ref_is_full_sha(std::string(40, '0')));
  CHECK(git_ref_is_full_sha("36cc599dca99520d2a0df22d62c4a87fc5a536d1"));
}

TEST_CASE("git_ref_is_full_sha accepts 64-char sha256 hex") {
  CHECK(git_ref_is_full_sha(std::string(64, 'f')));
}

TEST_CASE("git_ref_is_full_sha accepts uppercase hex") {
  CHECK(git_ref_is_full_sha("36CC599DCA99520D2A0DF22D62C4A87FC5A536D1"));
}

TEST_CASE("git_ref_is_full_sha rejects wrong lengths") {
  CHECK_FALSE(git_ref_is_full_sha(""));
  CHECK_FALSE(git_ref_is_full_sha(std::string(39, 'a')));
  CHECK_FALSE(git_ref_is_full_sha(std::string(41, 'a')));
  CHECK_FALSE(git_ref_is_full_sha(std::string(63, 'a')));
  CHECK_FALSE(git_ref_is_full_sha(std::string(7, 'a')));  // abbreviated sha
}

TEST_CASE("git_ref_is_full_sha rejects non-hex characters") {
  CHECK_FALSE(git_ref_is_full_sha(std::string(39, 'a') + "g"));  // g not hex
  CHECK_FALSE(git_ref_is_full_sha("refs/tags/siso/v1.5.23"));
  CHECK_FALSE(git_ref_is_full_sha(std::string(39, 'a') + "-"));
}

// ---------------------------------------------------------------------------
// git_resolve_ref -- exact matching
// ---------------------------------------------------------------------------

TEST_CASE("git_resolve_ref empty ref throws") {
  std::vector<git_ref_entry> refs{ { "refs/heads/main", kOidA } };
  CHECK_THROWS_AS(git_resolve_ref(refs, ""), std::runtime_error);
}

TEST_CASE("git_resolve_ref resolves an exact lightweight tag") {
  std::vector<git_ref_entry> refs{
    { "refs/heads/main", kOidA },
    { "refs/tags/v1.0", kOidB },
  };
  CHECK(git_resolve_ref(refs, "refs/tags/v1.0") == kOidB);
}

TEST_CASE("git_resolve_ref resolves an exact branch") {
  std::vector<git_ref_entry> refs{ { "refs/heads/main", kOidA } };
  CHECK(git_resolve_ref(refs, "refs/heads/main") == kOidA);
}

TEST_CASE("git_resolve_ref resolves HEAD") {
  std::vector<git_ref_entry> refs{ { "HEAD", kOidC }, { "refs/heads/main", kOidC } };
  CHECK(git_resolve_ref(refs, "HEAD") == kOidC);
}

TEST_CASE("git_resolve_ref prefers the peeled commit of an annotated tag") {
  // Annotated tag: plain entry is the tag object; "^{}" is the commit.
  std::vector<git_ref_entry> refs{
    { "refs/tags/v1.0", kOidA },      // tag object
    { "refs/tags/v1.0^{}", kOidB },   // peeled commit
  };
  CHECK(git_resolve_ref(refs, "refs/tags/v1.0") == kOidB);
}

TEST_CASE("git_resolve_ref resolves a peel-only advertisement") {
  // Defensive: peeled entry present without its plain counterpart.
  std::vector<git_ref_entry> refs{ { "refs/tags/v1.0^{}", kOidB } };
  CHECK(git_resolve_ref(refs, "refs/tags/v1.0") == kOidB);
}

TEST_CASE("git_resolve_ref throws when the ref is absent") {
  std::vector<git_ref_entry> refs{ { "refs/heads/main", kOidA } };
  CHECK_THROWS_AS(git_resolve_ref(refs, "refs/tags/nope"), std::runtime_error);
}

TEST_CASE("git_resolve_ref throws on an empty advertisement") {
  std::vector<git_ref_entry> refs{};
  CHECK_THROWS_AS(git_resolve_ref(refs, "refs/heads/main"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// git_resolve_ref -- suffix matching
// ---------------------------------------------------------------------------

TEST_CASE("git_resolve_ref resolves a unique suffix match") {
  std::vector<git_ref_entry> refs{
    { "refs/heads/main", kOidA },
    { "refs/tags/siso/v1.5.23", kOidB },
  };
  CHECK(git_resolve_ref(refs, "v1.5.23") == kOidB);
  CHECK(git_resolve_ref(refs, "siso/v1.5.23") == kOidB);
}

TEST_CASE("git_resolve_ref peels a suffix-matched annotated tag") {
  std::vector<git_ref_entry> refs{
    { "refs/tags/siso/v1.5.23", kOidA },
    { "refs/tags/siso/v1.5.23^{}", kOidB },
  };
  CHECK(git_resolve_ref(refs, "v1.5.23") == kOidB);
}

TEST_CASE("git_resolve_ref suffix match must be a whole trailing segment") {
  // ref "1.0" must not match ".../v1.0" -- needle is "/1.0".
  std::vector<git_ref_entry> refs{ { "refs/tags/v1.0", kOidA } };
  CHECK_THROWS_AS(git_resolve_ref(refs, "1.0"), std::runtime_error);
}

TEST_CASE("git_resolve_ref throws when a suffix match is ambiguous across oids") {
  std::vector<git_ref_entry> refs{
    { "refs/tags/x", kOidA },
    { "refs/heads/x", kOidB },
  };
  CHECK_THROWS_AS(git_resolve_ref(refs, "x"), std::runtime_error);
}

TEST_CASE("git_resolve_ref ambiguous message lists the competing refs") {
  std::vector<git_ref_entry> refs{
    { "refs/tags/x", kOidA },
    { "refs/heads/x", kOidB },
  };
  try {
    git_resolve_ref(refs, "x");
    FAIL("expected throw");
  } catch (std::runtime_error const &e) {
    std::string const msg{ e.what() };
    CHECK(msg.find("refs/tags/x") != std::string::npos);
    CHECK(msg.find("refs/heads/x") != std::string::npos);
  }
}

TEST_CASE("git_resolve_ref suffix collisions on one oid are not ambiguous") {
  std::vector<git_ref_entry> refs{
    { "refs/tags/x", kOidA },
    { "refs/heads/x", kOidA },  // same target
  };
  CHECK(git_resolve_ref(refs, "x") == kOidA);
}

TEST_CASE("git_resolve_ref exact match wins over an otherwise-ambiguous suffix") {
  std::vector<git_ref_entry> refs{
    { "x", kOidA },              // exact
    { "refs/tags/x", kOidB },    // would suffix-match, different oid
  };
  CHECK(git_resolve_ref(refs, "x") == kOidA);
}

// ---------------------------------------------------------------------------
// git_resolve_ref -- conflicting advertisements
// ---------------------------------------------------------------------------

TEST_CASE("git_resolve_ref throws on conflicting plain oids for one name") {
  std::vector<git_ref_entry> refs{
    { "refs/heads/main", kOidA },
    { "refs/heads/main", kOidB },
  };
  CHECK_THROWS_AS(git_resolve_ref(refs, "refs/heads/main"), std::runtime_error);
}

TEST_CASE("git_resolve_ref tolerates a duplicate identical plain oid") {
  std::vector<git_ref_entry> refs{
    { "refs/heads/main", kOidA },
    { "refs/heads/main", kOidA },
  };
  CHECK(git_resolve_ref(refs, "refs/heads/main") == kOidA);
}

TEST_CASE("git_resolve_ref throws on conflicting peeled oids for one name") {
  std::vector<git_ref_entry> refs{
    { "refs/tags/v1.0", kOidA },
    { "refs/tags/v1.0^{}", kOidB },
    { "refs/tags/v1.0^{}", kOidC },
  };
  CHECK_THROWS_AS(git_resolve_ref(refs, "refs/tags/v1.0"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// git_resolve_ref -- realistic siso advertisement
// ---------------------------------------------------------------------------

TEST_CASE("git_resolve_ref resolves a realistic siso tag advertisement") {
  std::string const commit{ "36cc599dca99520d2a0df22d62c4a87fc5a536d1" };
  std::string const tagobj{ "0123456789abcdef0123456789abcdef01234567" };
  std::vector<git_ref_entry> refs{
    { "HEAD", kOidA },
    { "refs/heads/main", kOidA },
    { "refs/tags/siso/v1.5.22", kOidD },
    { "refs/tags/siso/v1.5.23", tagobj },
    { "refs/tags/siso/v1.5.23^{}", commit },
  };
  // Fully-qualified (as the fi.siso.lua spec passes it) -> peeled commit.
  CHECK(git_resolve_ref(refs, "refs/tags/siso/v1.5.23") == commit);
  // Convenience suffix form also lands on the commit.
  CHECK(git_resolve_ref(refs, "siso/v1.5.23") == commit);
}

// ---------------------------------------------------------------------------
// Lua binding -- sha passthrough and argument validation (no network)
// ---------------------------------------------------------------------------

TEST_CASE("envy.git_resolve returns a full sha unchanged without a lookup") {
  auto lua{ envy::sol_util_make_lua_state() };
  sol::table envy_table{ lua->create_table() };
  envy::lua_envy_git_install(envy_table);

  sol::protected_function resolve{ envy_table["git_resolve"] };
  std::string const sha{ "36cc599dca99520d2a0df22d62c4a87fc5a536d1" };

  // repo is required non-empty but is never contacted for a full sha.
  sol::protected_function_result r{ resolve("https://example.invalid/repo", sha) };
  REQUIRE(r.valid());
  CHECK(r.get<std::string>() == sha);
}

TEST_CASE("envy.git_resolve rejects empty repo and empty ref") {
  auto lua{ envy::sol_util_make_lua_state() };
  sol::table envy_table{ lua->create_table() };
  envy::lua_envy_git_install(envy_table);

  sol::protected_function resolve{ envy_table["git_resolve"] };
  CHECK_FALSE(resolve("", "refs/heads/main").valid());
  CHECK_FALSE(resolve("https://example.invalid/repo", "").valid());
}
