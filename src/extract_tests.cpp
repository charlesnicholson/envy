#include "extract.h"

#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
      auto const rel{ std::filesystem::relative(entry.path(), root) };
      files.push_back(rel.generic_string());
    }
  }
  std::ranges::sort(files);
  return files;
}

std::uint64_t sum_file_sizes(std::filesystem::path const &root) {
  std::uint64_t total{ 0 };
  for (auto const &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) { total += std::filesystem::file_size(entry.path()); }
  }
  return total;
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

TEST_CASE("extract with strip_components too large throws error") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/test.tar.gz") };

  envy::extract_options opts{ .strip_components = 10 };

  try {
    envy::extract(archive, dest, opts);
    FAIL("Expected exception to be thrown");
  } catch (std::runtime_error const &e) {
    std::string const msg{ e.what() };
    CHECK(msg.find("test.tar.gz") != std::string::npos);
    CHECK(msg.find("strip=10") != std::string::npos);
  }

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract flat archive with strip=0 succeeds") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/flat.tar.gz") };

  envy::extract_options opts{ .strip_components = 0 };
  auto const count{ envy::extract(archive, dest, opts) };

  CHECK(count == 3);

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 3);
  CHECK(files[0] == "file1.txt");
  CHECK(files[1] == "file2.txt");
  CHECK(files[2] == "file3.txt");

  std::filesystem::remove_all(dest);
}

TEST_CASE("extract flat archive with strip=1 throws error") {
  auto const dest{ make_temp_dir() };
  auto const archive{ std::filesystem::path("test_data/archives/flat.tar.gz") };

  envy::extract_options opts{ .strip_components = 1 };

  try {
    envy::extract(archive, dest, opts);
    FAIL("Expected exception - flat archive cannot be stripped");
  } catch (std::runtime_error const &e) {
    std::string const msg{ e.what() };
    CHECK(msg.find("flat.tar.gz") != std::string::npos);
    CHECK(msg.find("strip=1") != std::string::npos);
    CHECK(msg.find("0 files extracted") != std::string::npos);
  }

  std::filesystem::remove_all(dest);
}

TEST_CASE("compute_extract_totals counts uncompressed archive bytes and plain files") {
  auto const fetch_dir{ make_temp_dir() };
  auto const archive_src{ std::filesystem::path("test_data/archives/test.tar.gz") };
  auto const archive_dest{ fetch_dir / "test.tar.gz" };
  std::filesystem::copy_file(archive_src, archive_dest);

  // Add a plain file (11 bytes)
  auto const plain{ fetch_dir / "plain.txt" };
  {
    std::ofstream out{ plain, std::ios::binary };
    out << "hello world";
  }

  // Ground truth: extract archive and sum uncompressed bytes
  auto const dest{ make_temp_dir() };
  envy::extract_options opts{ .strip_components = 0 };
  auto const files_in_archive{ envy::extract(archive_dest, dest, opts) };
  REQUIRE(files_in_archive == 5);
  std::uint64_t const archive_bytes{ sum_file_sizes(dest) };

  envy::extract_totals const totals{ envy::compute_extract_totals(fetch_dir) };

  CHECK(totals.files == 6);  // 5 from archive + 1 plain
  CHECK(totals.bytes == archive_bytes + 11);

  std::filesystem::remove_all(fetch_dir);
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

TEST_CASE("archive_create_tar_zst round-trip with files and directories") {
  auto const source{ make_temp_dir() };
  auto const dest{ make_temp_dir() };

  // Create source tree
  std::filesystem::create_directories(source / "subdir");
  { std::ofstream{ source / "file1.txt" } << "hello"; }
  { std::ofstream{ source / "subdir" / "file2.txt" } << "world"; }

  auto const archive{ make_temp_dir() / "test.tar.zst" };
  auto const files_archived{ envy::archive_create_tar_zst(archive, source, "pkg") };
  CHECK(files_archived == 2);
  CHECK(std::filesystem::exists(archive));
  CHECK(std::filesystem::file_size(archive) > 0);

  // Extract and verify contents under prefix
  envy::extract(archive, dest);

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 2);
  CHECK(files[0] == "pkg/file1.txt");
  CHECK(files[1] == "pkg/subdir/file2.txt");

  // Verify file contents survived round-trip
  {
    std::ifstream in{ dest / "pkg" / "file1.txt" };
    std::string content{ std::istreambuf_iterator<char>{ in }, {} };
    CHECK(content == "hello");
  }
  {
    std::ifstream in{ dest / "pkg" / "subdir" / "file2.txt" };
    std::string content{ std::istreambuf_iterator<char>{ in }, {} };
    CHECK(content == "world");
  }

  std::filesystem::remove_all(source);
  std::filesystem::remove_all(dest);
  std::filesystem::remove_all(archive.parent_path());
}

#ifndef _WIN32
TEST_CASE("archive_create_tar_zst preserves symlinks") {
  auto const source{ make_temp_dir() };
  auto const dest{ make_temp_dir() };

  // Create source with a symlink
  { std::ofstream{ source / "real.txt" } << "content"; }
  std::filesystem::create_symlink("real.txt", source / "link.txt");

  auto const archive{ make_temp_dir() / "symlink.tar.zst" };
  envy::archive_create_tar_zst(archive, source, "fetch");

  envy::extract(archive, dest);

  auto const link_path{ dest / "fetch" / "link.txt" };
  CHECK(std::filesystem::is_symlink(link_path));
  CHECK(std::filesystem::read_symlink(link_path) == "real.txt");

  std::filesystem::remove_all(source);
  std::filesystem::remove_all(dest);
  std::filesystem::remove_all(archive.parent_path());
}
#endif

TEST_CASE("archive_create_tar_zst with fetch prefix") {
  auto const source{ make_temp_dir() };
  auto const dest{ make_temp_dir() };

  { std::ofstream{ source / "archive.tar.gz" } << "fake archive data"; }

  auto const archive{ make_temp_dir() / "test.tar.zst" };
  envy::archive_create_tar_zst(archive, source, "fetch");

  envy::extract(archive, dest);

  auto const files{ collect_files_recursive(dest) };
  CHECK(files.size() == 1);
  CHECK(files[0] == "fetch/archive.tar.gz");

  std::filesystem::remove_all(source);
  std::filesystem::remove_all(dest);
  std::filesystem::remove_all(archive.parent_path());
}
