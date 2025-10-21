#include "tui.h"

#include "doctest.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("tui init can only run once") {
  CHECK_THROWS_AS(envy::tui::init(), std::logic_error);
}

TEST_CASE("tui allows handler changes while idle") {
  CHECK_NOTHROW(envy::tui::set_output_handler([](std::string_view) {}));
  CHECK_NOTHROW(envy::tui::set_output_handler([](std::string_view) {}));
}

TEST_CASE("tui enforces run/shutdown sequencing") {
  auto const handler{ [](std::string_view) {} };
  CHECK_NOTHROW(envy::tui::set_output_handler(handler));
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::INFO));
  CHECK_NOTHROW(envy::tui::shutdown());

  CHECK_NOTHROW(envy::tui::run(std::nullopt));
  CHECK_THROWS_AS(envy::tui::set_output_handler(handler), std::logic_error);
  CHECK_THROWS_AS(envy::tui::run(std::nullopt), std::logic_error);

  CHECK_NOTHROW(envy::tui::shutdown());
  CHECK_THROWS_AS(envy::tui::shutdown(), std::logic_error);

  CHECK_NOTHROW(envy::tui::set_output_handler(handler));
}

namespace {

struct captured_output {
  std::vector<std::string> messages;

  captured_output() {
    envy::tui::set_output_handler(
        [this](std::string_view value) { messages.emplace_back(value); });
  }

  ~captured_output() {
    try {
      envy::tui::set_output_handler([](std::string_view) {});
    } catch (std::logic_error const &error) {
      FAIL("set_output_handler should not throw during teardown: " << error.what());
    }
  }
};

}  // namespace

TEST_CASE_FIXTURE(captured_output, "tui unstructured logs are raw messages") {
  REQUIRE(messages.empty());

  CHECK_NOTHROW(envy::tui::run(std::nullopt));

  envy::tui::debug("hello %s", "world");
  envy::tui::info("value %d", 42);
  envy::tui::warn("three %d", 3);
  envy::tui::error("boom");

  CHECK_NOTHROW(envy::tui::shutdown());

  REQUIRE(messages.size() == 4);
  CHECK(messages[0] == "hello world\n");
  CHECK(messages[1] == "value 42\n");
  CHECK(messages[2] == "three 3\n");
  CHECK(messages[3] == "boom\n");
}

TEST_CASE_FIXTURE(captured_output, "tui structured logs include prefix") {
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::DEBUG));
  envy::tui::info("structured %d", 7);
  CHECK_NOTHROW(envy::tui::shutdown());

  REQUIRE(messages.size() == 1);
  auto const &line{ messages[0] };
  CHECK(line.find("[INF") != std::string::npos);
  CHECK(line.rfind("structured 7\n") == line.size() - std::string("structured 7\n").size());
}

TEST_CASE_FIXTURE(captured_output, "tui severity filtering honors threshold") {
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::WARN));
  envy::tui::debug("debug");
  envy::tui::info("info");
  envy::tui::warn("warn");
  envy::tui::error("error");
  CHECK_NOTHROW(envy::tui::shutdown());

  REQUIRE(messages.size() == 2);
  CHECK(messages[0].find("WRN") != std::string::npos);
  CHECK(messages[0].find("warn") != std::string::npos);
  CHECK(messages[1].find("ERR") != std::string::npos);
  CHECK(messages[1].find("error") != std::string::npos);

  messages.clear();
  CHECK_NOTHROW(envy::tui::run(envy::tui::level::INFO));
  envy::tui::debug("debug");
  envy::tui::info("info");
  CHECK_NOTHROW(envy::tui::shutdown());
  REQUIRE(messages.size() == 1);
  CHECK(messages[0].find("INF") != std::string::npos);
  CHECK(messages[0].find("info") != std::string::npos);
}
