#pragma once

// Test support utilities for functional testing
// Only available when ENVY_FUNCTIONAL_TESTER is defined

namespace envy::test {

// Fail-after-fetch-count: causes phase_fetch to fail after N successful downloads
// Returns current counter value (-1 = disabled)
int get_fail_after_fetch_count();
void set_fail_after_fetch_count(int count);

// Decrements counter and throws if it reaches zero
// Call after each successful file download in fetch phase
void decrement_fail_counter();

}  // namespace envy::test
