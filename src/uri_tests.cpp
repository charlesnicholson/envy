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
  // HTTPS git repos use GIT_HTTPS (require SSL certs)
  expect_uri("https://example.com/repo.git",
             envy::uri_scheme::GIT_HTTPS,
             "https://example.com/repo.git");
  expect_uri("https://example.com/repo.git?ref=main",
             envy::uri_scheme::GIT_HTTPS,
             "https://example.com/repo.git?ref=main");
  // Non-HTTPS git repos use GIT (no SSL certs needed)
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

// uri_extract_filename tests

TEST_CASE("extract_filename from simple HTTP URLs") {
  CHECK(envy::uri_extract_filename("https://example.com/archive.tar.gz") ==
        "archive.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/file.txt") == "file.txt");
  CHECK(envy::uri_extract_filename("https://cdn.example.org/gcc-13.2.0.tar.xz") ==
        "gcc-13.2.0.tar.xz");
}

TEST_CASE("extract_filename from URLs with paths") {
  CHECK(envy::uri_extract_filename("https://example.com/path/to/file.tar.gz") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/a/b/c/d/archive.zip") ==
        "archive.zip");
  CHECK(envy::uri_extract_filename("https://example.com/deep/nested/path/lib.so") ==
        "lib.so");
}

TEST_CASE("extract_filename strips query strings") {
  CHECK(envy::uri_extract_filename("https://example.com/file.tar.gz?version=1.0") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename(
            "http://example.com/archive.zip?token=abc123&user=foo") == "archive.zip");
  CHECK(envy::uri_extract_filename("https://example.com/path/file.txt?") == "file.txt");
}

TEST_CASE("extract_filename strips fragments") {
  CHECK(envy::uri_extract_filename("https://example.com/file.tar.gz#section") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/archive.zip#top") == "archive.zip");
  CHECK(envy::uri_extract_filename("https://example.com/path/file.txt#") == "file.txt");
}

TEST_CASE("extract_filename strips query and fragment") {
  CHECK(envy::uri_extract_filename("https://example.com/file.tar.gz?v=1#sec") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/path/archive.zip?foo=bar#baz") ==
        "archive.zip");
}

TEST_CASE("extract_filename handles percent encoding") {
  CHECK(envy::uri_extract_filename("https://example.com/my%20file.tar.gz") ==
        "my file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/file%2Bname.txt") ==
        "file+name.txt");
  CHECK(envy::uri_extract_filename("https://example.com/100%25.tar.gz") == "100%.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/path%2Ffile.txt") ==
        "path/file.txt");
}

TEST_CASE("extract_filename handles mixed case percent encoding") {
  CHECK(envy::uri_extract_filename("https://example.com/file%20name.txt") ==
        "file name.txt");
  CHECK(envy::uri_extract_filename("https://example.com/file%2ftest.txt") ==
        "file/test.txt");
  CHECK(envy::uri_extract_filename("https://example.com/file%2Ftest.txt") ==
        "file/test.txt");
}

TEST_CASE("extract_filename handles invalid percent encoding") {
  CHECK(envy::uri_extract_filename("https://example.com/file%2.txt") == "file%2.txt");
  CHECK(envy::uri_extract_filename("https://example.com/file%ZZ.txt") == "file%ZZ.txt");
  CHECK(envy::uri_extract_filename("https://example.com/file%.txt") == "file%.txt");
  CHECK(envy::uri_extract_filename("https://example.com/file%") == "file%");
}

TEST_CASE("extract_filename handles special characters") {
  CHECK(envy::uri_extract_filename("https://example.com/file-name.tar.gz") ==
        "file-name.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/file_name.txt") == "file_name.txt");
  CHECK(envy::uri_extract_filename("https://example.com/file.name.tar.gz") ==
        "file.name.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/file(1).zip") == "file(1).zip");
  CHECK(envy::uri_extract_filename("https://example.com/file[2].tar") == "file[2].tar");
}

TEST_CASE("extract_filename from URLs with trailing slash") {
  CHECK(envy::uri_extract_filename("https://example.com/path/") == "");
  CHECK(envy::uri_extract_filename("http://example.com/") == "");
  CHECK(envy::uri_extract_filename("https://example.com/path/to/dir/") == "");
}

TEST_CASE("extract_filename from URLs without path") {
  CHECK(envy::uri_extract_filename("https://example.com") == "example.com");
  CHECK(envy::uri_extract_filename("http://cdn.example.org") == "cdn.example.org");
  CHECK(envy::uri_extract_filename("ftp://ftp.example.net") == "ftp.example.net");
}

TEST_CASE("extract_filename from URLs with port numbers") {
  CHECK(envy::uri_extract_filename("https://example.com:8080/file.tar.gz") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com:443/path/archive.zip") ==
        "archive.zip");
  CHECK(envy::uri_extract_filename("ftp://example.com:21/file.txt") == "file.txt");
}

TEST_CASE("extract_filename from URLs with authentication") {
  CHECK(envy::uri_extract_filename("https://user:pass@example.com/file.tar.gz") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://admin@example.com/archive.zip") ==
        "archive.zip");
  CHECK(envy::uri_extract_filename("ftp://user:password@ftp.example.com/file.txt") ==
        "file.txt");
}

TEST_CASE("extract_filename from FTP URLs") {
  CHECK(envy::uri_extract_filename("ftp://ftp.example.com/pub/archive.tar.gz") ==
        "archive.tar.gz");
  CHECK(envy::uri_extract_filename("ftps://secure.example.com/files/data.zip") ==
        "data.zip");
}

TEST_CASE("extract_filename from S3 URLs") {
  CHECK(envy::uri_extract_filename("s3://bucket/path/to/file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("s3://my-bucket/archive.zip") == "archive.zip");
  CHECK(envy::uri_extract_filename("s3://bucket/deep/path/gcc.tar.xz") == "gcc.tar.xz");
}

TEST_CASE("extract_filename from Git URLs") {
  CHECK(envy::uri_extract_filename("git://github.com/org/repo.git") == "repo.git");
  CHECK(envy::uri_extract_filename("git+ssh://git@github.com/org/repo.git") == "repo.git");
  CHECK(envy::uri_extract_filename("https://github.com/user/project.git") ==
        "project.git");
}

TEST_CASE("extract_filename from SSH/SCP URLs") {
  CHECK(envy::uri_extract_filename("ssh://user@host/path/file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("scp://host/path/to/archive.zip") == "archive.zip");
  CHECK(envy::uri_extract_filename("git@github.com:org/repo.git") == "repo.git");
  CHECK(envy::uri_extract_filename("deploy@server.com:/var/toolchain.tar.xz") ==
        "toolchain.tar.xz");
}

TEST_CASE("extract_filename from file URLs") {
  CHECK(envy::uri_extract_filename("file:///tmp/archive.tar.gz") == "archive.tar.gz");
  CHECK(envy::uri_extract_filename("file://localhost/tmp/file.txt") == "file.txt");
  CHECK(envy::uri_extract_filename("file:///C:/tools/gcc.tar.xz") == "gcc.tar.xz");
}

TEST_CASE("extract_filename from relative paths") {
  CHECK(envy::uri_extract_filename("relative/path/file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("./local/archive.zip") == "archive.zip");
  CHECK(envy::uri_extract_filename("../parent/file.txt") == "file.txt");
  CHECK(envy::uri_extract_filename("path/to/dir/") == "");
}

TEST_CASE("extract_filename from absolute paths") {
  CHECK(envy::uri_extract_filename("/usr/local/bin/tool") == "tool");
  CHECK(envy::uri_extract_filename("/tmp/archive.tar.gz") == "archive.tar.gz");
  CHECK(envy::uri_extract_filename("/path/to/file.txt") == "file.txt");
}

TEST_CASE("extract_filename from Windows paths") {
  CHECK(envy::uri_extract_filename("C:/tools/gcc.tar.xz") == "gcc.tar.xz");
  CHECK(envy::uri_extract_filename("D:\\workspace\\archive.zip") == "archive.zip");
  CHECK(envy::uri_extract_filename("\\\\server\\share\\file.txt") == "file.txt");
}

TEST_CASE("extract_filename from bare filenames") {
  CHECK(envy::uri_extract_filename("archive.tar.gz") == "archive.tar.gz");
  CHECK(envy::uri_extract_filename("file.txt") == "file.txt");
  CHECK(envy::uri_extract_filename("gcc-13.2.0.tar.xz") == "gcc-13.2.0.tar.xz");
  CHECK(envy::uri_extract_filename("README") == "README");
  CHECK(envy::uri_extract_filename("Makefile") == "Makefile");
}

TEST_CASE("extract_filename from files with multiple extensions") {
  CHECK(envy::uri_extract_filename("https://example.com/file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/archive.tar.bz2") ==
        "archive.tar.bz2");
  CHECK(envy::uri_extract_filename("https://example.com/data.tar.xz") == "data.tar.xz");
  CHECK(envy::uri_extract_filename("http://example.com/lib.so.1.2.3") == "lib.so.1.2.3");
}

TEST_CASE("extract_filename from files without extension") {
  CHECK(envy::uri_extract_filename("https://example.com/README") == "README");
  CHECK(envy::uri_extract_filename("http://example.com/path/to/LICENSE") == "LICENSE");
  CHECK(envy::uri_extract_filename("https://example.com/Makefile") == "Makefile");
}

TEST_CASE("extract_filename handles empty and edge cases") {
  CHECK(envy::uri_extract_filename("") == "");
  CHECK(envy::uri_extract_filename("/") == "");
  CHECK(envy::uri_extract_filename("//") == "");
  CHECK(envy::uri_extract_filename("///") == "");
  CHECK(envy::uri_extract_filename("?query") == "");
  CHECK(envy::uri_extract_filename("#fragment") == "");
  CHECK(envy::uri_extract_filename("?") == "");
  CHECK(envy::uri_extract_filename("#") == "");
}

TEST_CASE("extract_filename from URLs with dots in path") {
  CHECK(envy::uri_extract_filename("https://example.com/.hidden/file.tar.gz") ==
        "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/v1.0/archive.zip") ==
        "archive.zip");
  CHECK(envy::uri_extract_filename("https://example.com/path.with.dots/file.txt") ==
        "file.txt");
}

TEST_CASE("extract_filename handles consecutive slashes") {
  CHECK(envy::uri_extract_filename("https://example.com//file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("http://example.com/path///archive.zip") ==
        "archive.zip");
  CHECK(envy::uri_extract_filename("https://example.com////file.txt") == "file.txt");
}

TEST_CASE("extract_filename from real-world examples") {
  CHECK(envy::uri_extract_filename(
            "https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/"
            "binrel/arm-gnu-toolchain-13.2.rel1-darwin-arm64-arm-none-eabi.tar.xz") ==
        "arm-gnu-toolchain-13.2.rel1-darwin-arm64-arm-none-eabi.tar.xz");

  CHECK(envy::uri_extract_filename(
            "https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.6/"
            "clang+llvm-17.0.6-x86_64-linux-gnu-ubuntu-22.04.tar.xz") ==
        "clang+llvm-17.0.6-x86_64-linux-gnu-ubuntu-22.04.tar.xz");

  CHECK(envy::uri_extract_filename(
            "https://nodejs.org/dist/v20.10.0/node-v20.10.0-darwin-arm64.tar.gz") ==
        "node-v20.10.0-darwin-arm64.tar.gz");

  CHECK(envy::uri_extract_filename(
            "https://pypi.org/simple/package-1.2.3.tar.gz#sha256=abc") ==
        "package-1.2.3.tar.gz");
}

TEST_CASE("extract_filename handles complex percent encoding") {
  CHECK(envy::uri_extract_filename("https://example.com/My%20Project%20v1.0.tar.gz") ==
        "My Project v1.0.tar.gz");
  CHECK(envy::uri_extract_filename("https://example.com/%5Btest%5D%20file.zip") ==
        "[test] file.zip");
  CHECK(envy::uri_extract_filename("https://example.com/file%28copy%29.tar.gz") ==
        "file(copy).tar.gz");
}

TEST_CASE("extract_filename preserves non-encoded special chars") {
  CHECK(envy::uri_extract_filename("https://example.com/file!name.tar.gz") ==
        "file!name.tar.gz");
  CHECK(envy::uri_extract_filename("https://example.com/file~test.zip") ==
        "file~test.zip");
  CHECK(envy::uri_extract_filename("https://example.com/file'name.tar.gz") ==
        "file'name.tar.gz");
}

TEST_CASE("extract_filename handles Unicode (already UTF-8 encoded)") {
  // URLs should contain percent-encoded UTF-8 sequences
  CHECK(envy::uri_extract_filename("https://example.com/%E6%96%87%E4%BB%B6.tar.gz") ==
        "文件.tar.gz");
  CHECK(envy::uri_extract_filename(
            "https://example.com/%D0%BF%D1%80%D0%B8%D0%B2%D0%B5%D1%82.zip") ==
        "привет.zip");
}

TEST_CASE("extract_filename from UNC paths") {
  CHECK(envy::uri_extract_filename("//server/share/file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("\\\\server\\share\\archive.zip") == "archive.zip");
}

TEST_CASE("extract_filename with mixed slashes and backslashes") {
  CHECK(envy::uri_extract_filename("C:/path\\to/file.tar.gz") == "file.tar.gz");
  CHECK(envy::uri_extract_filename("path/to\\file.zip") == "file.zip");
}
