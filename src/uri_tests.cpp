#include "uri.h"

#include "doctest.h"

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace {

std::filesystem::path normalize(std::filesystem::path const &p) {
  return std::filesystem::absolute(p).lexically_normal();
}

envy::uri_info expect_uri(std::string_view input,
                          envy::uri_scheme expected_scheme,
                          std::string_view expected_canonical) {
  auto info{ envy::uri_classify(input) };
  CHECK(info.scheme == expected_scheme);
  CHECK(info.canonical == expected_canonical);
  return info;
}

}  // namespace

TEST_CASE("classify_uri detects git via suffix") {
  expect_uri("https://example.com/repo.git",
             envy::uri_scheme::GIT,
             "https://example.com/repo.git");
  expect_uri("https://example.com/repo.git?ref=main",
             envy::uri_scheme::GIT,
             "https://example.com/repo.git?ref=main");
  expect_uri("git@github.com:org/repo.git",
             envy::uri_scheme::GIT,
             "git@github.com:org/repo.git");
  expect_uri("relative/repo.git", envy::uri_scheme::GIT, "relative/repo.git");
}

TEST_CASE("classify_uri detects explicit git schemes") {
  expect_uri("git://example.com/repo", envy::uri_scheme::GIT, "git://example.com/repo");
  expect_uri("git+ssh://example.com/repo",
             envy::uri_scheme::GIT,
             "git+ssh://example.com/repo");
}

TEST_CASE("classify_uri detects http schemes") {
  expect_uri("http://example.com/archive.tar.gz",
             envy::uri_scheme::HTTP,
             "http://example.com/archive.tar.gz");
  expect_uri("https://example.com/archive.tar.gz",
             envy::uri_scheme::HTTPS,
             "https://example.com/archive.tar.gz");
  expect_uri("HTTPS://EXAMPLE.COM/FILE",
             envy::uri_scheme::HTTPS,
             "HTTPS://EXAMPLE.COM/FILE");
}

TEST_CASE("classify_uri detects ftp schemes") {
  expect_uri("ftp://example.com/archive.tar.gz",
             envy::uri_scheme::FTP,
             "ftp://example.com/archive.tar.gz");
  expect_uri("ftps://example.com/archive.tar.gz",
             envy::uri_scheme::FTPS,
             "ftps://example.com/archive.tar.gz");
}

TEST_CASE("classify_uri detects s3 and ssh transports") {
  expect_uri("s3://bucket/object", envy::uri_scheme::S3, "s3://bucket/object");
  expect_uri("ssh://user@host/path/file.tar.gz",
             envy::uri_scheme::SSH,
             "ssh://user@host/path/file.tar.gz");
  expect_uri("scp://host/path/file.tar.gz",
             envy::uri_scheme::SSH,
             "scp://host/path/file.tar.gz");
  expect_uri("deploy@host.example.com:/var/archive/toolchain.tar.xz",
             envy::uri_scheme::SSH,
             "deploy@host.example.com:/var/archive/toolchain.tar.xz");
}

TEST_CASE("classify_uri detects local file schemes") {
#ifdef _WIN32
  expect_uri("file:///tmp/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "\\tmp\\archive.tar.gz");
  expect_uri("file://localhost/tmp/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "\\tmp\\archive.tar.gz");
#else
  expect_uri("file:///tmp/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "/tmp/archive.tar.gz");
  expect_uri("file://localhost/tmp/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "/tmp/archive.tar.gz");
#endif
#ifdef _WIN32
  expect_uri("file:///C:/toolchains/gcc.tar.xz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "C:/toolchains/gcc.tar.xz");
  expect_uri("file:///D:/workspace/assets/data.bin",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "D:/workspace/assets/data.bin");
  expect_uri("file://C:/direct/path.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "C:/direct/path.tar.gz");
#else
  expect_uri("file:///C:/toolchains/gcc.tar.xz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "C:/toolchains/gcc.tar.xz");
  expect_uri("file:///D:/workspace/assets/data.bin",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "D:/workspace/assets/data.bin");
  expect_uri("file://C:/direct/path.tar.gz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "C:/direct/path.tar.gz");
#endif
  expect_uri("file://server/share/toolchain.tar.xz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "//server/share/toolchain.tar.xz");
  expect_uri("file:////server/share/toolchain.tar.xz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "//server/share/toolchain.tar.xz");
}

TEST_CASE("classify_uri detects local file paths") {
#ifdef _WIN32
  expect_uri("/absolute/path/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "\\absolute\\path\\archive.tar.gz");
#else
  expect_uri("/absolute/path/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "/absolute/path/archive.tar.gz");
#endif
  expect_uri("relative/path/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "relative/path/archive.tar.gz");
  expect_uri("./relative/path/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "./relative/path/archive.tar.gz");
  expect_uri("../relative/path/archive.tar.gz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "../relative/path/archive.tar.gz");
#ifdef _WIN32
  expect_uri("C:\\toolchains\\arm.tar.xz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "C:\\toolchains\\arm.tar.xz");
  expect_uri("D:/workspace/assets/data.bin",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "D:/workspace/assets/data.bin");
  expect_uri("\\\\server\\share\\toolchain.tar.xz",
             envy::uri_scheme::LOCAL_FILE_ABSOLUTE,
             "\\\\server\\share\\toolchain.tar.xz");
#else
  expect_uri("C:\\toolchains\\arm.tar.xz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "C:\\toolchains\\arm.tar.xz");
  expect_uri("D:/workspace/assets/data.bin",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "D:/workspace/assets/data.bin");
  expect_uri("\\\\server\\share\\toolchain.tar.xz",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "\\\\server\\share\\toolchain.tar.xz");
#endif
  expect_uri("file://server:1234/assets/tool.lua",
             envy::uri_scheme::LOCAL_FILE_RELATIVE,
             "server:1234/assets/tool.lua");
}

TEST_CASE("classify_uri handles whitespace and unknown schemes") {
  expect_uri("  https://example.com/archive.tar.gz  ",
             envy::uri_scheme::HTTPS,
             "https://example.com/archive.tar.gz");
  expect_uri("unknown://example.com/resource",
             envy::uri_scheme::UNKNOWN,
             "unknown://example.com/resource");
  expect_uri("", envy::uri_scheme::UNKNOWN, "");
  expect_uri("   ", envy::uri_scheme::UNKNOWN, "");
}

TEST_CASE("resolve_local_uri resolves relative paths with manifest root") {
  auto const manifest_root{ std::filesystem::current_path() / "manifests/project" };
  auto const resolved{ envy::uri_resolve_local_file_relative("assets/archive.tar.gz",
                                                             manifest_root) };

  CHECK(resolved == normalize(manifest_root / "assets/archive.tar.gz"));
}

TEST_CASE("resolve_local_uri resolves relative paths without root") {
  auto const expected{ normalize("relative/file.txt") };
  auto const resolved{ envy::uri_resolve_local_file_relative("relative/file.txt",
                                                             std::nullopt) };

  CHECK(resolved == expected);
}

TEST_CASE("resolve_local_uri resolves file URIs") {
  auto const manifest_root{ std::filesystem::current_path() / "projects/sample" };
  auto const absolute_path{ normalize(std::filesystem::path("/tmp/data.bin")) };

  CHECK(envy::uri_resolve_local_file_relative("file:///tmp/data.bin", manifest_root) ==
        absolute_path);
  CHECK(envy::uri_resolve_local_file_relative("file://localhost/tmp/data.bin",
                                              manifest_root) == absolute_path);
}

TEST_CASE("resolve_local_uri handles Windows drive file URIs") {
  auto const manifest_root{ std::filesystem::current_path() / "projects/sample" };

#ifdef _WIN32
  CHECK(envy::uri_resolve_local_file_relative("file:///C:/toolchains/gcc.tar.xz",
                                              manifest_root) ==
        normalize(std::filesystem::path("C:/toolchains/gcc.tar.xz")));

  CHECK(envy::uri_resolve_local_file_relative("file:///D:/workspace/assets/data.bin",
                                              manifest_root) ==
        normalize(std::filesystem::path("D:/workspace/assets/data.bin")));

  CHECK(envy::uri_resolve_local_file_relative("file://C:/direct/path.tar.gz",
                                              manifest_root) ==
        normalize(std::filesystem::path("C:/direct/path.tar.gz")));
#else
  CHECK(envy::uri_resolve_local_file_relative("file:///C:/toolchains/gcc.tar.xz",
                                              manifest_root) ==
        normalize(manifest_root / "C:/toolchains/gcc.tar.xz"));

  CHECK(envy::uri_resolve_local_file_relative("file:///D:/workspace/assets/data.bin",
                                              manifest_root) ==
        normalize(manifest_root / "D:/workspace/assets/data.bin"));

  CHECK(envy::uri_resolve_local_file_relative("file://C:/direct/path.tar.gz",
                                              manifest_root) ==
        normalize(manifest_root / "C:/direct/path.tar.gz"));
#endif
}

TEST_CASE("resolve_local_uri rejects non-local values") {
  CHECK_THROWS_AS(
      envy::uri_resolve_local_file_relative("https://example.com/archive.tar.gz",
                                            std::nullopt),
      std::invalid_argument);
}

TEST_CASE("resolve_local_uri rejects empty inputs") {
  CHECK_THROWS_AS(envy::uri_resolve_local_file_relative("", std::nullopt),
                  std::invalid_argument);
  CHECK_THROWS_AS(
      envy::uri_resolve_local_file_relative("   ", std::filesystem::current_path()),
      std::invalid_argument);
}

TEST_CASE("resolve_local_uri handles minimal file URIs") {
  auto const manifest_root{ std::filesystem::current_path() / "projects/sample" };
  CHECK_THROWS_AS(envy::uri_resolve_local_file_relative("file://", manifest_root),
                  std::invalid_argument);
  auto const root_result{ envy::uri_resolve_local_file_relative("file:///",
                                                                manifest_root) };
  CHECK(root_result.is_absolute());
  CHECK(root_result == normalize(std::filesystem::path("/")));
}

TEST_CASE("resolve_local_uri preserves absolute local paths") {
#ifdef _WIN32
  auto const path{ std::filesystem::path("C:/tools/bin/utility.exe") };
  CHECK(envy::uri_resolve_local_file_relative("C:/tools/bin/utility.exe", std::nullopt) ==
        normalize(path));
#else
  auto const path{ std::filesystem::path("/usr/local/bin/tool") };
  CHECK(envy::uri_resolve_local_file_relative("/usr/local/bin/tool", std::nullopt) ==
        normalize(path));
#endif
}

TEST_CASE("resolve_local_uri handles UNC-style hosts") {
  auto const manifest_root{ std::filesystem::current_path() / "projects/sample" };
#ifdef _WIN32
  auto const expected{ std::filesystem::path("\\\\server\\share\\toolchain.tar.xz") };
#else
  auto const expected{ std::filesystem::path("//server/share/toolchain.tar.xz") };
#endif
  CHECK(envy::uri_resolve_local_file_relative("file://server/share/toolchain.tar.xz",
                                              manifest_root) == normalize(expected));
  CHECK(envy::uri_resolve_local_file_relative("file:////server/share/toolchain.tar.xz",
                                              manifest_root) == normalize(expected));
}
