// Unit tests for engine target phase promotion and notification.

#include "engine.h"

#include "doctest.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace envy {

// Helper to test recipe_execution_ctx behavior
struct test_ctx {
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<recipe_phase> target_phase{ recipe_phase::recipe_fetch };

  void set_target_phase(recipe_phase target) {
    recipe_phase current_target = target_phase.load();
    while (current_target < target) {
      if (target_phase.compare_exchange_weak(current_target, target)) {
        std::lock_guard const lock(mutex);
        cv.notify_one();
        return;
      }
    }
  }
};

TEST_CASE("engine: set_target_phase promotes and notifies") {
  test_ctx ctx;
  bool woken = false;

  std::thread waiter([&] {
    std::unique_lock lock{ ctx.mutex };
    ctx.cv.wait(lock, [&] { return ctx.target_phase.load() >= recipe_phase::completion; });
    woken = true;
  });

  ctx.set_target_phase(recipe_phase::completion);
  waiter.join();
  CHECK(ctx.target_phase.load() == recipe_phase::completion);
  CHECK(woken);
}

TEST_CASE("engine: set_target_phase is idempotent when already reached") {
  test_ctx ctx;
  ctx.target_phase = recipe_phase::completion;
  ctx.set_target_phase(recipe_phase::asset_check);
  CHECK(ctx.target_phase.load() == recipe_phase::completion);
}

TEST_CASE("engine: set_target_phase promotes from none to check") {
  test_ctx ctx;
  ctx.target_phase = recipe_phase::none;
  ctx.set_target_phase(recipe_phase::asset_check);
  CHECK(ctx.target_phase.load() == recipe_phase::asset_check);
}

}  // namespace envy
