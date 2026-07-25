#include "reexec.h"

#include "doctest.h"

// --- reexec_should decision logic ---

TEST_CASE("reexec_should: no @envy version returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::nullopt, false, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: dev build 0.0.0 returns PROCEED") {
  CHECK(envy::reexec_should("0.0.0", std::string{ "1.5.0" }, false, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: version match returns PROCEED") {
  CHECK(envy::reexec_should("1.5.0", std::string{ "1.5.0" }, false, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: ENVY_REEXEC set returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, true, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: ENVY_NO_REEXEC set returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, false, true) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: both ENVY_REEXEC and ENVY_NO_REEXEC set returns PROCEED") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, true, true) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: version mismatch (downgrade) returns REEXEC") {
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, false, false) ==
        envy::reexec_decision::REEXEC);
}

TEST_CASE("reexec_should: version mismatch (upgrade) returns REEXEC") {
  CHECK(envy::reexec_should("1.0.0", std::string{ "2.0.0" }, false, false) ==
        envy::reexec_decision::REEXEC);
}

TEST_CASE("reexec_should: empty requested version string triggers REEXEC") {
  // optional with empty string is still a value; "" != "2.0.0" → mismatch
  CHECK(envy::reexec_should("2.0.0", std::string{ "" }, false, false) ==
        envy::reexec_decision::REEXEC);
}

TEST_CASE("reexec_should: dev build 0.0.0 even with REEXEC flag returns PROCEED") {
  // Dev build check comes before REEXEC flag check — 0.0.0 always wins
  CHECK(envy::reexec_should("0.0.0", std::string{ "1.5.0" }, true, false) ==
        envy::reexec_decision::PROCEED);
}

TEST_CASE("reexec_should: ENVY_NO_REEXEC takes priority over version mismatch") {
  // no_reexec is checked before version comparison
  CHECK(envy::reexec_should("2.0.0", std::string{ "1.5.0" }, false, true) ==
        envy::reexec_decision::PROCEED);
}
