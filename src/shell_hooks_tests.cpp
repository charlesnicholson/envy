#include "shell_hooks.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

struct temp_dir_fixture {
  temp_dir_fixture() {
    static std::mt19937_64 rng{ std::random_device{}() };
    root = std::filesystem::temp_directory_path() /
           ("envy-shell-hooks-test-" + std::to_string(rng()));
    std::filesystem::create_directories(root);
  }

  ~temp_dir_fixture() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  void write_file(std::filesystem::path const &p, std::string_view content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out{ p, std::ios::binary };
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }

  std::filesystem::path root;
};

}  // namespace

// --- parse_version_from_content ---

TEST_CASE("shell_hooks: parse_version_from_content") {
  using envy::shell_hooks::parse_version_from_content;

  SUBCASE("bash format: _ENVY_HOOK_VERSION=1") {
    CHECK(parse_version_from_content("# comment\n_ENVY_HOOK_VERSION=1\n") == 1);
  }

  SUBCASE("bash format: _ENVY_HOOK_VERSION=42") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=42\n") == 42);
  }

  SUBCASE("powershell format: $global:_ENVY_HOOK_VERSION = 1") {
    CHECK(parse_version_from_content("$global:_ENVY_HOOK_VERSION = 1\n") == 1);
  }

  SUBCASE("fish format: set -g _ENVY_HOOK_VERSION 3") {
    CHECK(parse_version_from_content("set -g _ENVY_HOOK_VERSION 3\n") == 3);
  }

  SUBCASE("spaces around equals: _ENVY_HOOK_VERSION = 5") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION = 5\n") == 5);
  }

  SUBCASE("no trailing newline") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=7") == 7);
  }

  SUBCASE("empty content returns 0") { CHECK(parse_version_from_content("") == 0); }

  SUBCASE("no version stamp returns 0") {
    CHECK(parse_version_from_content("# just a comment\necho hello\n") == 0);
  }

  SUBCASE("version on line 5 (last checked) is found") {
    CHECK(parse_version_from_content(
              "line1\nline2\nline3\nline4\n_ENVY_HOOK_VERSION=9\n") == 9);
  }

  SUBCASE("version on line 6 (beyond limit) is not found") {
    CHECK(parse_version_from_content("1\n2\n3\n4\n5\n_ENVY_HOOK_VERSION=9\n") == 0);
  }

  SUBCASE("non-numeric value returns 0") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=abc\n") == 0);
  }

  SUBCASE("version 0 explicitly") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=0\n") == 0);
  }

  SUBCASE("negative version parses as negative") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=-1\n") == -1);
  }

  SUBCASE("large version number") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=999\n") == 999);
  }

  SUBCASE("version with trailing text: _ENVY_HOOK_VERSION=3 # comment") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=3 # comment\n") == 3);
  }

  SUBCASE("multiple equals signs: _ENVY_HOOK_VERSION==2") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION==2\n") == 2);
  }

  SUBCASE("only spaces after key: _ENVY_HOOK_VERSION   8") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION   8\n") == 8);
  }

  SUBCASE("partial match _ENVY_HOOK_VERSIONX is still found (substring)") {
    // _ENVY_HOOK_VERSION is found as substring; "X" follows, skipping '=' and spaces
    // finds no digit -> returns 0
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSIONX=1\n") == 0);
  }

  SUBCASE("first occurrence wins if multiple present") {
    CHECK(parse_version_from_content("_ENVY_HOOK_VERSION=2\n_ENVY_HOOK_VERSION=5\n") == 2);
  }

  SUBCASE("real bash hook header") {
    CHECK(parse_version_from_content("# envy shell hook — managed by envy; do not edit\n"
                                     "_ENVY_HOOK_VERSION=1\n"
                                     "\n"
                                     "_envy_find_manifest() {\n") == 1);
  }

  SUBCASE("real fish hook header") {
    CHECK(parse_version_from_content("# envy shell hook — managed by envy; do not edit\n"
                                     "set -g _ENVY_HOOK_VERSION 1\n") == 1);
  }

  SUBCASE("real powershell hook header") {
    CHECK(parse_version_from_content("# envy shell hook — managed by envy; do not edit\n"
                                     "$global:_ENVY_HOOK_VERSION = 1\n") == 1);
  }
}

// --- parse_version (file-based) ---

TEST_CASE_FIXTURE(temp_dir_fixture, "shell_hooks: parse_version") {
  using envy::shell_hooks::parse_version;

  SUBCASE("nonexistent file returns 0") {
    CHECK(parse_version(root / "nonexistent") == 0);
  }

  SUBCASE("valid hook file returns version") {
    auto const p{ root / "hook.bash" };
    write_file(p, "# comment\n_ENVY_HOOK_VERSION=3\n");
    CHECK(parse_version(p) == 3);
  }

  SUBCASE("empty file returns 0") {
    auto const p{ root / "empty" };
    write_file(p, "");
    CHECK(parse_version(p) == 0);
  }

  SUBCASE("file with no stamp returns 0") {
    auto const p{ root / "no_stamp" };
    write_file(p, "echo hello\necho world\n");
    CHECK(parse_version(p) == 0);
  }

  SUBCASE("file with stamp beyond line 5 returns 0") {
    auto const p{ root / "late_stamp" };
    write_file(p, "1\n2\n3\n4\n5\n_ENVY_HOOK_VERSION=7\n");
    CHECK(parse_version(p) == 0);
  }
}

// --- kVersion constant ---

TEST_CASE("shell_hooks: kVersion is positive") { CHECK(envy::shell_hooks::kVersion > 0); }

// --- ensure ---

TEST_CASE_FIXTURE(temp_dir_fixture, "shell_hooks: ensure") {
  namespace fs = std::filesystem;
  using envy::shell_hooks::ensure;
  using envy::shell_hooks::kVersion;
  using envy::shell_hooks::parse_version;

  SUBCASE("creates all 4 hook files in empty cache") {
    int const written{ ensure(root) };
    CHECK(written == 4);
    CHECK(fs::exists(root / "shell" / "hook.bash"));
    CHECK(fs::exists(root / "shell" / "hook.zsh"));
    CHECK(fs::exists(root / "shell" / "hook.fish"));
    CHECK(fs::exists(root / "shell" / "hook.ps1"));
  }

  SUBCASE("written hooks have correct version") {
    ensure(root);
    CHECK(parse_version(root / "shell" / "hook.bash") == kVersion);
    CHECK(parse_version(root / "shell" / "hook.zsh") == kVersion);
    CHECK(parse_version(root / "shell" / "hook.fish") == kVersion);
    CHECK(parse_version(root / "shell" / "hook.ps1") == kVersion);
  }

  SUBCASE("second ensure writes nothing (already up-to-date)") {
    ensure(root);
    int const written{ ensure(root) };
    CHECK(written == 0);
  }

  SUBCASE("stale hooks are updated") {
    ensure(root);
    auto const bash_hook{ root / "shell" / "hook.bash" };
    write_file(bash_hook, "# old\n_ENVY_HOOK_VERSION=0\nold content\n");
    int const written{ ensure(root) };
    CHECK(written == 1);  // only the stale one
    CHECK(parse_version(bash_hook) == kVersion);
  }

  SUBCASE("hooks with missing version stamp are rewritten") {
    ensure(root);
    auto const zsh_hook{ root / "shell" / "hook.zsh" };
    write_file(zsh_hook, "# broken hook with no version\necho hi\n");
    int const written{ ensure(root) };
    CHECK(written == 1);
    CHECK(parse_version(zsh_hook) == kVersion);
  }

  SUBCASE("hooks at current version are not rewritten") {
    ensure(root);
    auto const fish_hook{ root / "shell" / "hook.fish" };
    auto const mtime_before{ fs::last_write_time(fish_hook) };
    ensure(root);
    auto const mtime_after{ fs::last_write_time(fish_hook) };
    CHECK(mtime_before == mtime_after);
  }

  SUBCASE("hooks with future version are not downgraded") {
    fs::create_directories(root / "shell");
    auto const hook{ root / "shell" / "hook.bash" };
    write_file(hook, "# future\n_ENVY_HOOK_VERSION=999\n");
    int const written{ ensure(root) };
    // bash should be skipped (version 999 >= kVersion), other 3 created
    CHECK(written == 3);
    CHECK(parse_version(hook) == 999);
  }

  SUBCASE("written hooks contain managed-by comment") {
    ensure(root);
    for (auto const *ext : { "bash", "zsh", "fish", "ps1" }) {
      auto const hook{ root / "shell" / ("hook." + std::string{ ext }) };
      std::ifstream in{ hook };
      std::string content{ std::istreambuf_iterator<char>{ in },
                           std::istreambuf_iterator<char>{} };
      CHECK_MESSAGE(content.find("managed by envy") != std::string::npos,
                    "missing managed-by comment in hook.",
                    ext);
    }
  }
}
