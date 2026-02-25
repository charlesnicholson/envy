#include "platform.h"

#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace envy {

TEST_CASE("platform::get_exe_path returns valid path") {
  auto const path{ platform::get_exe_path() };

  CHECK(!path.empty());
  CHECK(path.is_absolute());
  CHECK(std::filesystem::exists(path));
  CHECK(std::filesystem::is_regular_file(path));
}

TEST_CASE("platform::get_exe_path returns executable file") {
  auto const path{ platform::get_exe_path() };
  auto const filename{ path.filename().string() };

  // Should be one of our test executables
  CHECK((filename.find("envy") != std::string::npos ||
         filename.find("test") != std::string::npos));
}

TEST_CASE("platform::expand_path empty path returns empty path") {
  auto result{ platform::expand_path("") };
  CHECK(result.empty());
}

TEST_CASE("platform::expand_path plain path returns unchanged") {
  auto result{ platform::expand_path("/absolute/path/to/something") };
  CHECK(result == "/absolute/path/to/something");
}

TEST_CASE("platform::expand_path relative path returns unchanged") {
  auto result{ platform::expand_path("relative/path") };
  CHECK(result == "relative/path");
}

TEST_CASE("platform::os_name returns expected value") {
  auto const os{ platform::os_name() };
  CHECK(!os.empty());
#if defined(__APPLE__) && defined(__MACH__)
  CHECK(os == "darwin");
#elif defined(__linux__)
  CHECK(os == "linux");
#elif defined(_WIN32)
  CHECK(os == "windows");
#endif
}

TEST_CASE("platform::arch_name returns expected value") {
  auto const arch{ platform::arch_name() };
  CHECK(!arch.empty());
#if defined(__arm64__) || defined(_M_ARM64)
  CHECK((arch == "arm64" || arch == "aarch64"));
#elif defined(__aarch64__)
  CHECK(arch == "aarch64");
#elif defined(__x86_64__) || defined(_M_X64)
  CHECK(arch == "x86_64");
#endif
}

#ifndef _WIN32
TEST_CASE("platform::expand_path tilde expands to HOME") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);  // HOME should always be set on Unix

  auto result{ platform::expand_path("~") };
  CHECK(result == std::filesystem::path{ home });
}

TEST_CASE("platform::expand_path tilde slash expands correctly") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  auto result{ platform::expand_path("~/foo/bar") };
  CHECK(result == std::filesystem::path{ home } / "foo" / "bar");
}

TEST_CASE("platform::expand_path with env var expands correctly") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  auto result{ platform::expand_path("$HOME/test") };
  CHECK(result == std::filesystem::path{ home } / "test");
}

TEST_CASE("platform::expand_path with braced env var expands correctly") {
  char const *home{ std::getenv("HOME") };
  REQUIRE(home != nullptr);

  auto result{ platform::expand_path("${HOME}/test") };
  CHECK(result == std::filesystem::path{ home } / "test");
}
#endif

TEST_CASE("platform::native returns expected value") {
  auto const id{ platform::native() };
#ifdef _WIN32
  CHECK(id == platform_id::WINDOWS);
#else
  CHECK(id == platform_id::POSIX);
#endif
}

TEST_CASE("platform::native is consistent with platform::os_name") {
  auto const id{ platform::native() };
  auto const os{ platform::os_name() };
  if (os == "windows") {
    CHECK(id == platform_id::WINDOWS);
  } else {
    CHECK(id == platform_id::POSIX);
  }
}

TEST_CASE("platform::exe_suffix returns platform-correct suffix") {
#ifdef _WIN32
  CHECK(platform::exe_suffix() == ".exe");
#else
  CHECK(platform::exe_suffix() == "");
#endif
}

TEST_CASE("platform::exe_name appends suffix to base name") {
  auto const name{ platform::exe_name("envy") };
#ifdef _WIN32
  CHECK(name == std::filesystem::path{ "envy.exe" });
#else
  CHECK(name == std::filesystem::path{ "envy" });
#endif
}

TEST_CASE("platform::exe_name works with arbitrary base names") {
  auto const name{ platform::exe_name("cmake") };
#ifdef _WIN32
  CHECK(name == std::filesystem::path{ "cmake.exe" });
#else
  CHECK(name == std::filesystem::path{ "cmake" });
#endif
}

#ifdef _WIN32

namespace {

struct temp_dir {
  temp_dir() {
    static std::mt19937_64 rng{ std::random_device{}() };
    root = std::filesystem::temp_directory_path() /
           ("envy-platform-test-" + std::to_string(rng()));
    std::filesystem::create_directories(root);
  }
  ~temp_dir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  std::filesystem::path root;
};

}  // namespace

TEST_CASE("remove_all_with_retry: succeeds on nonexistent target") {
  temp_dir t;
  auto const target{ t.root / "does-not-exist" };
  auto const ec{ platform::remove_all_with_retry(target) };
  CHECK(!ec);
}

TEST_CASE("remove_all_with_retry: removes a normal directory tree") {
  temp_dir t;
  auto const target{ t.root / "tree" };
  std::filesystem::create_directories(target / "sub");
  { std::ofstream{ target / "sub" / "file.txt" } << "data"; }
  auto const ec{ platform::remove_all_with_retry(target) };
  CHECK(!ec);
  CHECK(!std::filesystem::exists(target));
}

TEST_CASE("remove_all_with_retry: returns success when locked file is "
          "released before probe") {
  // Simulate: lock a file inside target, call remove_all_with_retry (which
  // will fail on the locked file during retries), release the lock while
  // retries are still running, and verify it eventually returns success.
  temp_dir t;
  auto const target{ t.root / "locked" };
  std::filesystem::create_directories(target);
  auto const locked_file{ target / "held.bin" };
  { std::ofstream{ locked_file } << "payload"; }

  // Open with exclusive access (no sharing) to simulate Defender lock.
  HANDLE h{ ::CreateFileW(locked_file.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0,  // no sharing — exclusive
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr) };
  REQUIRE(h != INVALID_HANDLE_VALUE);

  // Release after 150ms — within the retry window (~3.5s) but after the
  // first attempt (which sleeps 50ms).
  std::thread releaser{ [h] {
    ::Sleep(150);
    ::CloseHandle(h);
  } };

  auto const ec{ platform::remove_all_with_retry(target) };
  releaser.join();

  CHECK(!ec);
  CHECK(!std::filesystem::exists(target));
}

TEST_CASE("remove_all_with_retry: post-loop probe detects target gone") {
  // Verify the post-loop existence check: create a dir, delete it
  // externally, then confirm remove_all_with_retry on a path that no
  // longer exists returns success.
  temp_dir t;
  auto const target{ t.root / "vanish" };
  std::filesystem::create_directories(target);
  // Remove it before calling — simulates the race where the target
  // disappears between the last remove_all error and the probe.
  std::filesystem::remove(target);
  auto const ec{ platform::remove_all_with_retry(target) };
  CHECK(!ec);
}

#endif  // _WIN32

}  // namespace envy
