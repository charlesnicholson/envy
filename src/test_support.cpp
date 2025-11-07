#include "test_support.h"

#include <atomic>
#include <stdexcept>

namespace envy::test {

namespace {
std::atomic<int> fail_after_fetch_count_{ -1 };
}

int get_fail_after_fetch_count() { return fail_after_fetch_count_.load(); }

void set_fail_after_fetch_count(int count) { fail_after_fetch_count_.store(count); }

void decrement_fail_counter() {
  int current = fail_after_fetch_count_.load();
  if (current > 0) {
    int remaining = fail_after_fetch_count_.fetch_sub(1);
    if (remaining == 1) {  // We just decremented from 1 to 0
      throw std::runtime_error("TEST: fail_after_fetch_count triggered");
    }
  }
}

}  // namespace envy::test
