#include "fetch.h"

#include "doctest.h"

#include <filesystem>
#include <optional>
#include <stdexcept>

TEST_CASE("fetch rejects empty sources") {
  envy::fetch_request request{
    .source = "",
    .destination = std::filesystem::path("ignored"),
    .file_root = std::nullopt,
    .progress = {}
  };

  CHECK_THROWS_AS(envy::fetch(request), std::invalid_argument);
}

TEST_CASE("fetch rejects unsupported schemes") {
  envy::fetch_request request{
    .source = "foo://bucket/object",
    .destination = std::filesystem::path("ignored"),
    .file_root = std::nullopt,
    .progress = {}
  };

  CHECK_THROWS_AS(envy::fetch(request), std::runtime_error);
}

