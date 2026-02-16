#include "cli.h"
#include "cmds/cmd_extract.h"
#include "cmds/cmd_fetch.h"
#include "cmds/cmd_hash.h"
#include "cmds/cmd_init.h"
#include "cmds/cmd_install.h"
#include "cmds/cmd_lua.h"
#include "cmds/cmd_package.h"
#include "cmds/cmd_product.h"
#include "cmds/cmd_run.h"
#include "cmds/cmd_shell.h"
#include "cmds/cmd_sync.h"
#include "cmds/cmd_version.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

namespace {

// Helper to convert vector of strings to argc/argv
std::vector<char *> make_argv(std::vector<std::string> &args) {
  std::vector<char *> argv;
  for (auto &arg : args) { argv.push_back(arg.data()); }
  argv.push_back(nullptr);
  return argv;
}

}  // anonymous namespace

TEST_CASE("cli_parse: no arguments") {
  std::vector<std::string> args{ "envy" };
  auto argv{ make_argv(args) };

  auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

  // With no arguments, help text returned and no command configuration.
  CHECK_FALSE(parsed.cmd_cfg.has_value());
}

TEST_CASE("cli_parse: cmd_version") {
  SUBCASE("-v flag") {
    std::vector<std::string> args{ "envy", "-v" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK(std::holds_alternative<envy::cmd_version::cfg>(*parsed.cmd_cfg));
  }

  SUBCASE("--version flag") {
    std::vector<std::string> args{ "envy", "--version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK(std::holds_alternative<envy::cmd_version::cfg>(*parsed.cmd_cfg));
  }

  SUBCASE("version subcommand without --licenses") {
    std::vector<std::string> args{ "envy", "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_version::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK_FALSE(cfg->show_licenses);
  }

  SUBCASE("version --licenses flag") {
    std::vector<std::string> args{ "envy", "version", "--licenses" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_version::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->show_licenses);
  }
}

TEST_CASE("cli_parse: cmd_extract") {
  SUBCASE("archive and destination") {
    // Create temporary test archive
    auto temp_archive{ std::filesystem::temp_directory_path() /
                       "cli_test_archive.tar.gz" };
    auto temp_dest{ std::filesystem::temp_directory_path() / "cli_test_dest" };
    {
      std::ofstream temp_file{ temp_archive };
      temp_file << "fake archive\n";
    }

    std::vector<std::string> args{ "envy",
                                   "extract",
                                   temp_archive.string(),
                                   temp_dest.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_archive);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_extract::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->archive_path == temp_archive);
    CHECK(cfg->destination == temp_dest);
  }

  SUBCASE("archive without destination") {
    // Create temporary test archive
    auto temp_archive{ std::filesystem::temp_directory_path() /
                       "cli_test_archive2.tar.gz" };
    {
      std::ofstream temp_file{ temp_archive };
      temp_file << "fake archive\n";
    }

    std::vector<std::string> args{ "envy", "extract", temp_archive.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_archive);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_extract::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->archive_path == temp_archive);
    CHECK(cfg->destination.empty());
  }
}

TEST_CASE("cli_parse: cmd_fetch") {
  SUBCASE("basic fetch command") {
    std::vector<std::string> args{ "envy",
                                   "fetch",
                                   "https://example.com/archive.tar.gz",
                                   "/tmp/local.tar.gz" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_fetch::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->source == "https://example.com/archive.tar.gz");
    CHECK(cfg->destination == std::filesystem::path("/tmp/local.tar.gz"));
    CHECK_FALSE(cfg->manifest_root.has_value());
  }

  SUBCASE("fetch with manifest root") {
    std::vector<std::string> args{ "envy",
                                   "fetch",
                                   "file://relative/path/tool.tar.gz",
                                   "/tmp/tool.tar.gz",
                                   "--manifest-root",
                                   "/workspace" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_fetch::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->source == "file://relative/path/tool.tar.gz");
    CHECK(cfg->destination == std::filesystem::path("/tmp/tool.tar.gz"));
    REQUIRE(cfg->manifest_root.has_value());
    CHECK(*cfg->manifest_root == std::filesystem::path("/workspace"));
  }
}

TEST_CASE("cli_parse: cmd_hash") {
  SUBCASE("with valid file") {
    // Create temporary test file
    auto temp_path{ std::filesystem::temp_directory_path() / "cli_test_hash.txt" };
    {
      std::ofstream temp_file{ temp_path };
      temp_file << "test content\n";
    }

    std::vector<std::string> args{ "envy", "hash", temp_path.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_path);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_hash::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->file_path == temp_path);
  }

  SUBCASE("missing file path rejected") {
    std::vector<std::string> args{ "envy", "hash" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Should fail when file argument is missing
    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }

  SUBCASE("nonexistent file rejected") {
    std::vector<std::string> args{ "envy", "hash", "/nonexistent/file.txt" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // CLI11's ExistingFile check should reject nonexistent files
    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }

  SUBCASE("directory rejected") {
    // Use temp directory which we know exists and is a directory
    auto temp_dir{ std::filesystem::temp_directory_path() };

    std::vector<std::string> args{ "envy", "hash", temp_dir.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // CLI11's ExistingFile check should reject directories
    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }
}

TEST_CASE("cli_parse: cmd_lua") {
  SUBCASE("with script path") {
    // Create temporary test file
    auto temp_path{ std::filesystem::temp_directory_path() / "cli_test_script.lua" };
    {
      std::ofstream temp_file{ temp_path };
      temp_file << "-- test script\n";
    }

    std::vector<std::string> args{ "envy", "lua", temp_path.string() };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    // Clean up temp file
    std::filesystem::remove(temp_path);

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_lua::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->script_path == temp_path);
  }
}

// TEST_CASE removed: cmd_playground has been deleted

TEST_CASE("cli_parse: cmd_package") {
  SUBCASE("identity only") {
    std::vector<std::string> args{ "envy", "package", "vendor.gcc@v2" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_package::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->identity == "vendor.gcc@v2");
    CHECK_FALSE(cfg->manifest_path.has_value());
  }

  SUBCASE("with manifest") {
    std::vector<std::string> args{ "envy",
                                   "package",
                                   "vendor.gcc@v2",
                                   "--manifest",
                                   "/path/to/envy.lua" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_package::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->identity == "vendor.gcc@v2");
    REQUIRE(cfg->manifest_path.has_value());
    CHECK(cfg->manifest_path->string() == "/path/to/envy.lua");
  }
}

TEST_CASE("cli_parse: cmd_product") {
  SUBCASE("product only") {
    std::vector<std::string> args{ "envy", "product", "tool" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_product::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->product_name == "tool");
    CHECK_FALSE(cfg->manifest_path.has_value());
    CHECK_FALSE(cfg->json);
  }

  SUBCASE("with manifest") {
    std::vector<std::string> args{ "envy",
                                   "product",
                                   "tool",
                                   "--manifest",
                                   "/tmp/envy.lua" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_product::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->product_name == "tool");
    REQUIRE(cfg->manifest_path.has_value());
    CHECK(*cfg->manifest_path == std::filesystem::path("/tmp/envy.lua"));
    CHECK_FALSE(cfg->json);
  }

  SUBCASE("no product name lists all") {
    std::vector<std::string> args{ "envy", "product" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_product::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->product_name.empty());
    CHECK_FALSE(cfg->json);
  }

  SUBCASE("json flag enabled") {
    std::vector<std::string> args{ "envy", "product", "--json" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_product::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->product_name.empty());
    CHECK(cfg->json);
  }

  SUBCASE("json with product name") {
    std::vector<std::string> args{ "envy", "product", "tool", "--json" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_product::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->product_name == "tool");
    CHECK(cfg->json);
  }
}

TEST_CASE("cli_parse: verbose flag") {
  std::vector<std::string> args{ "envy", "--verbose", "version" };
  auto argv{ make_argv(args) };

  auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

  REQUIRE(parsed.cmd_cfg.has_value());
  REQUIRE(parsed.verbosity.has_value());
  CHECK(parsed.verbosity == envy::tui::level::TUI_DEBUG);
  CHECK(parsed.decorated_logging);
}

TEST_CASE("cli_parse: trace flag enables structured outputs") {
  SUBCASE("stderr trace explicit") {
    std::vector<std::string> args{ "envy", "--trace=stderr", "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    REQUIRE(parsed.verbosity.has_value());
    CHECK(parsed.verbosity == envy::tui::level::TUI_TRACE);
    CHECK(parsed.decorated_logging);
    REQUIRE(parsed.trace_outputs.size() == 1);
    CHECK(parsed.trace_outputs[0].type == envy::tui::trace_output_type::std_err);
    CHECK_FALSE(parsed.trace_outputs[0].file_path.has_value());
  }

  SUBCASE("file trace target") {
    std::filesystem::path const trace_path{ "/tmp/envy-trace.jsonl" };
    std::vector<std::string> args{ "envy",
                                   "--trace=file:" + trace_path.string(),
                                   "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    REQUIRE(parsed.verbosity.has_value());
    CHECK(parsed.verbosity == envy::tui::level::TUI_TRACE);
    REQUIRE(parsed.trace_outputs.size() == 1);
    CHECK(parsed.trace_outputs[0].type == envy::tui::trace_output_type::file);
    REQUIRE(parsed.trace_outputs[0].file_path.has_value());
    CHECK(parsed.trace_outputs[0].file_path->string() == trace_path.string());
  }

  SUBCASE("multiple trace destinations") {
    std::filesystem::path const trace_path{ "/tmp/envy-trace.jsonl" };
    std::vector<std::string> args{ "envy",
                                   "--trace=stderr,file:" + trace_path.string(),
                                   "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    REQUIRE(parsed.verbosity.has_value());
    CHECK(parsed.verbosity == envy::tui::level::TUI_TRACE);
    REQUIRE(parsed.trace_outputs.size() == 2);
    CHECK(parsed.trace_outputs[0].type == envy::tui::trace_output_type::std_err);
    CHECK_FALSE(parsed.trace_outputs[0].file_path.has_value());
    CHECK(parsed.trace_outputs[1].type == envy::tui::trace_output_type::file);
    REQUIRE(parsed.trace_outputs[1].file_path.has_value());
    CHECK(parsed.trace_outputs[1].file_path->string() == trace_path.string());
  }

  SUBCASE("invalid trace spec rejected") {
    std::vector<std::string> args{ "envy", "--trace=bogus", "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
    CHECK(parsed.trace_outputs.empty());
  }
}

TEST_CASE("cli_parse: global cache-root flag") {
  SUBCASE("no cache-root by default") {
    std::vector<std::string> args{ "envy", "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cache_root.has_value());
  }

  SUBCASE("cache-root with value") {
    std::vector<std::string> args{ "envy", "--cache-root", "/tmp/cache", "version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    REQUIRE(parsed.cache_root.has_value());
    CHECK(*parsed.cache_root == std::filesystem::path("/tmp/cache"));
  }

  SUBCASE("cache-root works with sync command") {
    std::vector<std::string> args{ "envy", "--cache-root", "/my/cache", "sync" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    REQUIRE(parsed.cache_root.has_value());
    CHECK(*parsed.cache_root == std::filesystem::path("/my/cache"));
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
  }
}

TEST_CASE("cli_parse: cmd_install") {
  SUBCASE("no arguments (install all)") {
    std::vector<std::string> args{ "envy", "install" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_install::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->queries.empty());
    CHECK_FALSE(cfg->manifest_path.has_value());
  }

  SUBCASE("with queries") {
    std::vector<std::string> args{ "envy", "install", "gcc", "binutils" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_install::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->queries.size() == 2);
    CHECK(cfg->queries[0] == "gcc");
    CHECK(cfg->queries[1] == "binutils");
  }

  SUBCASE("with --manifest") {
    std::vector<std::string> args{ "envy", "install", "--manifest", "/path/to/envy.lua" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_install::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->queries.empty());
    REQUIRE(cfg->manifest_path.has_value());
    CHECK(*cfg->manifest_path == std::filesystem::path("/path/to/envy.lua"));
  }

  SUBCASE("queries with --manifest") {
    std::vector<std::string> args{ "envy",
                                   "install",
                                   "gcc",
                                   "--manifest",
                                   "/path/to/envy.lua" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_install::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->queries.size() == 1);
    CHECK(cfg->queries[0] == "gcc");
    REQUIRE(cfg->manifest_path.has_value());
    CHECK(*cfg->manifest_path == std::filesystem::path("/path/to/envy.lua"));
  }
}

TEST_CASE("cli_parse: cmd_sync flags") {
  SUBCASE("default flags") {
    std::vector<std::string> args{ "envy", "sync" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK_FALSE(cfg->strict);
    CHECK_FALSE(cfg->subproject);
  }

  SUBCASE("--strict flag") {
    std::vector<std::string> args{ "envy", "sync", "--strict" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->strict);
  }

  SUBCASE("--subproject flag") {
    std::vector<std::string> args{ "envy", "sync", "--subproject" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->subproject);
  }

  SUBCASE("with identities") {
    std::vector<std::string> args{ "envy", "sync", "pkg1", "pkg2" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->identities.size() == 2);
    CHECK(cfg->identities[0] == "pkg1");
    CHECK(cfg->identities[1] == "pkg2");
  }

  SUBCASE("--manifest flag") {
    std::vector<std::string> args{ "envy", "sync", "--manifest", "/path/to/envy.lua" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->manifest_path.has_value());
    CHECK(*cfg->manifest_path == std::filesystem::path("/path/to/envy.lua"));
  }

  SUBCASE("--subproject with --manifest rejected") {
    std::vector<std::string> args{ "envy",
                                   "sync",
                                   "--subproject",
                                   "--manifest",
                                   "/path/to/envy.lua" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }
}

TEST_CASE("cli_parse: cmd_shell") {
  SUBCASE("bash") {
    std::vector<std::string> args{ "envy", "shell", "bash" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_shell::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->shell == "bash");
  }

  SUBCASE("zsh") {
    std::vector<std::string> args{ "envy", "shell", "zsh" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_shell::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->shell == "zsh");
  }

  SUBCASE("fish") {
    std::vector<std::string> args{ "envy", "shell", "fish" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_shell::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->shell == "fish");
  }

  SUBCASE("powershell") {
    std::vector<std::string> args{ "envy", "shell", "powershell" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_shell::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->shell == "powershell");
  }

  SUBCASE("missing shell argument") {
    std::vector<std::string> args{ "envy", "shell" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }

  SUBCASE("invalid shell rejected") {
    std::vector<std::string> args{ "envy", "shell", "csh" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }
}

TEST_CASE("cli_parse: cmd_run") {
  SUBCASE("basic command") {
    std::vector<std::string> args{ "envy", "run", "ls" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_run::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->command.size() == 1);
    CHECK(cfg->command[0] == "ls");
  }

  SUBCASE("command with arguments") {
    std::vector<std::string> args{ "envy", "run", "python3", "-c", "print(1)" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_run::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->command.size() == 3);
    CHECK(cfg->command[0] == "python3");
    CHECK(cfg->command[1] == "-c");
    CHECK(cfg->command[2] == "print(1)");
  }

  SUBCASE("no command") {
    std::vector<std::string> args{ "envy", "run" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_run::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->command.empty());
  }

  SUBCASE("child flags not intercepted") {
    std::vector<std::string> args{ "envy", "run", "grep", "--version" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_run::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    REQUIRE(cfg->command.size() == 2);
    CHECK(cfg->command[0] == "grep");
    CHECK(cfg->command[1] == "--version");
  }
}

TEST_CASE("cli_parse: cmd_sync --platform") {
  SUBCASE("default (no --platform)") {
    std::vector<std::string> args{ "envy", "sync" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag.empty());
  }

  SUBCASE("--platform posix") {
    std::vector<std::string> args{ "envy", "sync", "--platform", "posix" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag == "posix");
  }

  SUBCASE("--platform windows") {
    std::vector<std::string> args{ "envy", "sync", "--platform", "windows" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag == "windows");
  }

  SUBCASE("--platform all") {
    std::vector<std::string> args{ "envy", "sync", "--platform", "all" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_sync::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag == "all");
  }

  SUBCASE("invalid --platform value rejected") {
    std::vector<std::string> args{ "envy", "sync", "--platform", "linux" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }
}

TEST_CASE("cli_parse: cmd_init --platform") {
  SUBCASE("default (no --platform)") {
    std::vector<std::string> args{ "envy", "init", "/tmp/proj", "/tmp/bin" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_init::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag.empty());
  }

  SUBCASE("--platform posix") {
    std::vector<std::string> args{ "envy",     "init",       "/tmp/proj",
                                   "/tmp/bin", "--platform", "posix" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_init::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag == "posix");
  }

  SUBCASE("--platform windows") {
    std::vector<std::string> args{ "envy",     "init",       "/tmp/proj",
                                   "/tmp/bin", "--platform", "windows" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_init::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag == "windows");
  }

  SUBCASE("--platform all") {
    std::vector<std::string> args{ "envy",     "init",       "/tmp/proj",
                                   "/tmp/bin", "--platform", "all" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    REQUIRE(parsed.cmd_cfg.has_value());
    auto const *cfg{ std::get_if<envy::cmd_init::cfg>(&*parsed.cmd_cfg) };
    REQUIRE(cfg != nullptr);
    CHECK(cfg->platform_flag == "all");
  }

  SUBCASE("invalid --platform value rejected") {
    std::vector<std::string> args{ "envy",     "init",       "/tmp/proj",
                                   "/tmp/bin", "--platform", "macos" };
    auto argv{ make_argv(args) };

    auto parsed{ envy::cli_parse(static_cast<int>(args.size()), argv.data()) };

    CHECK_FALSE(parsed.cmd_cfg.has_value());
    CHECK_FALSE(parsed.cli_output.empty());
  }
}
