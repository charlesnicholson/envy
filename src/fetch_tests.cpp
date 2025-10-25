#include "fetch.h"

#include "doctest.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace envy::detail {
std::filesystem::path resolve_local_source(
    std::string_view source,
    std::optional<std::filesystem::path> const &root);
}

namespace {

std::filesystem::path normalize(std::filesystem::path const &p) {
  return std::filesystem::absolute(p).lexically_normal();
}

}  // namespace

TEST_CASE("fetch_classify: git detection by suffix") {
  CHECK(envy::fetch_classify("https://example.com/repo.git") == envy::fetch_scheme::GIT);
  CHECK(envy::fetch_classify("https://example.com/repo.git?ref=main") ==
        envy::fetch_scheme::GIT);
  CHECK(envy::fetch_classify("git@github.com:org/repo.git") == envy::fetch_scheme::GIT);
  CHECK(envy::fetch_classify("relative/repo.git") == envy::fetch_scheme::GIT);
}

TEST_CASE("fetch_classify: explicit git schemes") {
  CHECK(envy::fetch_classify("git://example.com/repo") == envy::fetch_scheme::GIT);
  CHECK(envy::fetch_classify("git+ssh://example.com/repo") == envy::fetch_scheme::GIT);
}

TEST_CASE("fetch_classify: http family") {
  CHECK(envy::fetch_classify("http://example.com/archive.tar.gz") ==
        envy::fetch_scheme::HTTP);
  CHECK(envy::fetch_classify("https://example.com/archive.tar.gz") ==
        envy::fetch_scheme::HTTPS);
  CHECK(envy::fetch_classify("HTTPS://EXAMPLE.COM/FILE") == envy::fetch_scheme::HTTPS);
}

TEST_CASE("fetch_classify: ftp family") {
  CHECK(envy::fetch_classify("ftp://example.com/archive.tar.gz") ==
        envy::fetch_scheme::FTP);
  CHECK(envy::fetch_classify("ftps://example.com/archive.tar.gz") ==
        envy::fetch_scheme::FTPS);
}

TEST_CASE("fetch_classify: s3 and ssh transports") {
  CHECK(envy::fetch_classify("s3://bucket/object") == envy::fetch_scheme::S3);
  CHECK(envy::fetch_classify("ssh://user@host/path/file.tar.gz") ==
        envy::fetch_scheme::SSH);
  CHECK(envy::fetch_classify("scp://host/path/file.tar.gz") == envy::fetch_scheme::SSH);
  CHECK(envy::fetch_classify("deploy@host.example.com:/var/archive/toolchain.tar.xz") ==
        envy::fetch_scheme::SSH);
}

TEST_CASE("fetch_classify: local files") {
  CHECK(envy::fetch_classify("file:///tmp/archive.tar.gz") ==
        envy::fetch_scheme::LOCAL_FILE);
  CHECK(envy::fetch_classify("file://localhost/tmp/archive.tar.gz") ==
        envy::fetch_scheme::LOCAL_FILE);
  CHECK(envy::fetch_classify("relative/path/archive.tar.gz") ==
        envy::fetch_scheme::LOCAL_FILE);
  CHECK(envy::fetch_classify("/absolute/path/archive.tar.gz") ==
        envy::fetch_scheme::LOCAL_FILE);
  CHECK(envy::fetch_classify("C:\\toolchains\\arm.tar.xz") ==
        envy::fetch_scheme::LOCAL_FILE);
}

TEST_CASE("fetch_classify: whitespace and unknown") {
  CHECK(envy::fetch_classify("  https://example.com/archive.tar.gz  ") ==
        envy::fetch_scheme::HTTPS);
  CHECK(envy::fetch_classify("unknown://example.com/resource") ==
        envy::fetch_scheme::UNKNOWN);
  CHECK(envy::fetch_classify("") == envy::fetch_scheme::UNKNOWN);
  CHECK(envy::fetch_classify("   ") == envy::fetch_scheme::UNKNOWN);
}

TEST_CASE("fetch_resolve_file: manifest relative resolution") {
  auto const manifest_root{ std::filesystem::current_path() / "manifests/project" };
  auto const resolved{ envy::detail::resolve_local_source("assets/archive.tar.gz",
                                                          manifest_root) };
  CHECK(resolved == normalize(manifest_root / "assets/archive.tar.gz"));
}

TEST_CASE("fetch_resolve_file: cwd fallback") {
  auto const expected{ normalize("relative/file.txt") };
  auto const resolved{ envy::detail::resolve_local_source("relative/file.txt",
                                                          std::nullopt) };
  CHECK(resolved == expected);
}

TEST_CASE("fetch_resolve_file: file URI handling") {
  auto const manifest{ std::filesystem::current_path() / "projects/sample" };
  auto const absolute_path{ normalize(std::filesystem::path("/tmp/data.bin")) };
  CHECK(envy::detail::resolve_local_source("file:///tmp/data.bin", manifest) ==
        absolute_path);
  CHECK(envy::detail::resolve_local_source("file://localhost/tmp/data.bin", manifest) ==
        absolute_path);
}

TEST_CASE("fetch_resolve_file: file URI with Windows drive letter") {
  auto const manifest{ std::filesystem::current_path() / "projects/sample" };

#ifdef _WIN32
  CHECK(envy::detail::resolve_local_source("file:///C:/toolchains/gcc.tar.xz", manifest) ==
        normalize(std::filesystem::path("C:/toolchains/gcc.tar.xz")));

  CHECK(envy::detail::resolve_local_source("file:///D:/workspace/assets/data.bin",
                                           manifest) ==
        normalize(std::filesystem::path("D:/workspace/assets/data.bin")));

  CHECK(envy::detail::resolve_local_source("file://C:/direct/path.tar.gz", manifest) ==
        normalize(std::filesystem::path("C:/direct/path.tar.gz")));
#else
  CHECK(envy::detail::resolve_local_source("file:///C:/toolchains/gcc.tar.xz", manifest) ==
        normalize(manifest / "C:/toolchains/gcc.tar.xz"));

  CHECK(envy::detail::resolve_local_source("file:///D:/workspace/assets/data.bin",
                                           manifest) ==
        normalize(manifest / "D:/workspace/assets/data.bin"));

  CHECK(envy::detail::resolve_local_source("file://C:/direct/path.tar.gz", manifest) ==
        normalize(manifest / "C:/direct/path.tar.gz"));
#endif
}

TEST_CASE("fetch: empty source rejects") {
  envy::fetch_request request{
    .source = "",
    .destination = std::filesystem::path("ignored"),
    .file_root = std::nullopt,
    .progress = {}
  };

  CHECK_THROWS_AS(envy::fetch(request), std::invalid_argument);
}

TEST_CASE("fetch: unsupported scheme throws") {
  envy::fetch_request request{
    .source = "foo://bucket/object",
    .destination = std::filesystem::path("ignored"),
    .file_root = std::nullopt,
    .progress = {}
  };

  CHECK_THROWS_AS(envy::fetch(request), std::runtime_error);
}
