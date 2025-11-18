#include "test_support.h"

#include <atomic>
#include <stdexcept>

namespace envy::test {

namespace { std::atomic<int> fail_after_fetch_count_{ -1 }; }

int get_fail_after_fetch_count() { return fail_after_fetch_count_; }

void set_fail_after_fetch_count(int count) { fail_after_fetch_count_ = count; }

void decrement_fail_counter() {
  int expected = fail_after_fetch_count_;
  while (expected > 0) {
    if (fail_after_fetch_count_.compare_exchange_weak(expected, expected - 1)) {
      if (expected == 1) {
        throw std::runtime_error("TEST: fail_after_fetch_count triggered");
      }
      return;
    }
  }
}

}  // namespace envy::test
