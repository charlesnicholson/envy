#include "extract.h"

#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{ std::random_device{}() };
  auto suffix{ std::to_string(rng()) };
  auto dir{ std::filesystem::temp_directory_path() /
            std::filesystem::path("envy-extract-test-" + suffix) };
  std::filesystem::create_directories(dir);
  return dir;
}

std::vector<std::string> collect_files_recursive(std::filesystem::path const &root) {
  std::vector<std::string> files;
  for (auto const &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      auto rel{ std::filesystem::relative(entry.path(), root) };
        std::string s{ rel.string() };
        // Normalize Windows backslashes to forward slashes for stable comparisons.
        for (char &ch : s) { if (ch == '\\') ch = '/'; }
        files.push_back(s);
    }
  }
  std::ranges::sort(files);
  return files;
}

}  // namespace

TEST_CASE("extract with strip_components=0 preserves structure") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/test.tar.gz") };

  envy::extract_options opts{ .strip_components = 0 };
  auto const count{ envy::extract(archive, dest, opts) };

  CHECK(count == 5);  // 5 regular files

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 5);
  CHECK(files[0] == "root/file1.txt");
  CHECK(files[1] == "root/file2.txt");
  CHECK(files[2] == "root/subdir1/file3.txt");
  CHECK(files[3] == "root/subdir1/nested/file4.txt");
  CHECK(files[4] == "root/subdir2/file5.txt");

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract with strip_components=1 removes top-level directory") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/test.tar.gz") };

  envy::extract_options opts{ .strip_components = 1 };
  auto const count{ envy::extract(archive, dest, opts) };

  CHECK(count == 5);

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 5);
  CHECK(files[0] == "file1.txt");
  CHECK(files[1] == "file2.txt");
  CHECK(files[2] == "subdir1/file3.txt");
  CHECK(files[3] == "subdir1/nested/file4.txt");
  CHECK(files[4] == "subdir2/file5.txt");

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract with strip_components=2 removes two levels") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/test.tar.gz") };

  envy::extract_options opts{ .strip_components = 2 };
  auto const count{ envy::extract(archive, dest, opts) };

  // Only files at least 2 levels deep are extracted
  CHECK(count == 3);

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 3);
  CHECK(files[0] == "file3.txt");
  CHECK(files[1] == "file5.txt");
  CHECK(files[2] == "nested/file4.txt");

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract with strip_components=3 extracts deeply nested only") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/test.tar.gz") };

  envy::extract_options opts{ .strip_components = 3 };
  auto const count{ envy::extract(archive, dest, opts) };

  // Only file4.txt is at least 3 levels deep (root/subdir1/nested/file4.txt)
  CHECK(count == 1);

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 1);
  CHECK(files[0] == "file4.txt");

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract with strip_components too large extracts nothing") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/test.tar.gz") };

  envy::extract_options opts{ .strip_components = 10 };
  auto const count{ envy::extract(archive, dest, opts) };

  CHECK(count == 0);  // No files deep enough

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.empty());

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract different archive formats with strip") {
  auto const test_archives{ std::vector<std::string>{
      "test.tar",
      "test.tar.bz2",
      "test.tar.gz",
      "test.tar.xz",
      "test.tar.zst"
      // Skip test.zip - structure might differ
  } };

  for (auto const &archive_name : test_archives) {
    auto const dest{ make_temp_dir() };
    auto const archive{ std::filesystem::path("test_data/archives") / archive_name };

    envy::extract_options opts{ .strip_components = 1 };
    auto const count{ envy::extract(archive, dest, opts) };

    CHECK(count == 5);

    auto const files{ collect_files_recursive(dest) };
    CHECK(files.size() == 5);

    std::filesystem::remove_all(dest);
  }
}
