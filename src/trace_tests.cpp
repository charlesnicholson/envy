#include "trace.h"

#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

// Every event type must serialize to a well-formed JSON object carrying the shared
// "ts"/"event" keys and its own name. Iterating the variant by index means a newly
// added event type is covered automatically; the static_assert below forces the
// expected-count sentinel to be revisited whenever the variant grows or shrinks.
template <std::size_t I>
void check_alternative_serializes() {
  using alternative = std::variant_alternative_t<I, envy::trace_event_t>;
  envy::trace_event_t const event{ std::in_place_index<I>, alternative{} };

  auto const name{ envy::trace_event_name(event) };
  CHECK_FALSE(name.empty());
  CHECK(name != "unknown");

  auto const json{ envy::trace_event_to_json(event) };
  REQUIRE(json.size() >= 2);
  CHECK(json.front() == '{');
  CHECK(json.back() == '}');
  CHECK(json.find("\"ts\"") != std::string::npos);
  CHECK(json.find(std::string{ "\"event\":\"" } + std::string(name) + "\"") !=
        std::string::npos);
}

template <std::size_t... Is>
void check_all(std::index_sequence<Is...>) {
  (check_alternative_serializes<Is>(), ...);
}

}  // namespace

TEST_CASE("trace_event_to_json emits valid JSON for every event type") {
  constexpr std::size_t kEventCount{ std::variant_size_v<envy::trace_event_t> };
  static_assert(kEventCount == 28,
                "trace_event_t changed: confirm the new/removed event serializes and "
                "update this count");
  check_all(std::make_index_sequence<kEventCount>{});
}

TEST_CASE("trace_event_name is unique per event type") {
  constexpr std::size_t kEventCount{ std::variant_size_v<envy::trace_event_t> };
  std::vector<std::string> names;
  auto collect{ [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    (names.emplace_back(envy::trace_event_name(envy::trace_event_t{
         std::in_place_index<Is>, std::variant_alternative_t<Is, envy::trace_event_t>{} })),
     ...);
  } };
  collect(std::make_index_sequence<kEventCount>{});

  std::sort(names.begin(), names.end());
  CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
}
