#include "fetch.h"

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("fetch rejects empty sources") {
  envy::fetch_request_file request{ .source = "",
                                    .destination = std::filesystem::path("ignored"),
                                    .file_root = {} };

  auto const results{ envy::fetch({ request }) };
  REQUIRE(results.size() == 1);
  REQUIRE(std::holds_alternative<std::string>(results[0]));
  CHECK(std::get<std::string>(results[0]).find("empty") != std::string::npos);
}

TEST_CASE("fetch rejects unsupported schemes") {
  envy::fetch_request_http request{ .source = "foo://bucket/object",
                                    .destination = std::filesystem::path("ignored") };

  auto const results{ envy::fetch({ request }) };
  REQUIRE(results.size() == 1);
  REQUIRE(std::holds_alternative<std::string>(results[0]));
  // The error message should mention an error occurred
  auto const &error = std::get<std::string>(results[0]);
  INFO("Actual error message: ", error);
  CHECK(!error.empty());
}

TEST_CASE("fetch batch download multiple files") {
  auto const temp_dir = std::filesystem::temp_directory_path() / "fetch_test_batch";
  std::filesystem::create_directories(temp_dir);

  // Create source files
  auto const source1 = temp_dir / "source1.txt";
  auto const source2 = temp_dir / "source2.txt";
  auto const source3 = temp_dir / "source3.txt";

  std::ofstream(source1) << "content one";
  std::ofstream(source2) << "content two";
  std::ofstream(source3) << "content three";

  // Create destination directory
  auto const dest_dir = temp_dir / "dest";
  std::filesystem::create_directories(dest_dir);

  // Batch fetch all three files
  std::vector<envy::fetch_request> requests{
    envy::fetch_request_file{ .source = source1.string(),
                              .destination = dest_dir / "dest1.txt" },
    envy::fetch_request_file{ .source = source2.string(),
                              .destination = dest_dir / "dest2.txt" },
    envy::fetch_request_file{ .source = source3.string(),
                              .destination = dest_dir / "dest3.txt" }
  };

  auto const results = envy::fetch(requests);

  REQUIRE(results.size() == 3);

  for (size_t i = 0; i < results.size(); ++i) {
    INFO("Checking result ", i);
    REQUIRE(std::holds_alternative<envy::fetch_result>(results[i]));
    auto const &result = std::get<envy::fetch_result>(results[i]);
    CHECK(std::filesystem::exists(result.resolved_destination));
  }

  CHECK(std::filesystem::exists(dest_dir / "dest1.txt"));
  CHECK(std::filesystem::exists(dest_dir / "dest2.txt"));
  CHECK(std::filesystem::exists(dest_dir / "dest3.txt"));

  // Cleanup
  std::filesystem::remove_all(temp_dir);
}
