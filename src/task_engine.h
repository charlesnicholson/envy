#pragma once

#include "util.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace envy {

// Generic threaded task executor. A task is an interned, keyed, linear sequence
// of steps run on a dedicated worker thread. Tasks advance toward a ratcheting
// target watermark; per-step edges block a step until another task reaches a
// watermark. Tasks may be created at any time, including from other tasks'
// step callbacks — the graph grows while it runs.
//
// Watermark semantics: watermark N is reached once the task has completed its
// first N steps. "Done" is watermark step_count. A step callback may finish the
// task early, jumping straight to done.
//
// Domain-agnostic: keys are opaque strings, steps/edges are callbacks. Blocking
// inside step callbacks is legal — workers are plain threads, not pooled.
//
// Lock ordering: engine mutex_ may be held while acquiring a task mutex, never
// the reverse. Callbacks are always invoked with no engine locks held.
class task_engine : unmovable {
 public:
  struct edge {
    std::string key;     // task to wait for
    int watermark{ 0 };  // completed-step count that satisfies the wait
    // Ratchet the target at least this far (max'd with watermark). Lets a
    // dependent demand extra downstream work without waiting for it — e.g. a
    // dependency runs its export step concurrently with the dependent.
    int extend_to{ 0 };
  };

  struct task_config {
    std::string key;
    int step_count{ 0 };

    // Run step `i` (0-based). Return true to finish the task early (remaining
    // steps skipped, watermark jumps to step_count). Throw to fail the task.
    std::function<bool(int step)> step;

    // Edges that must be satisfied before step `i` runs. Queried on the worker
    // thread immediately before each step, so results may grow with the graph.
    // Every returned target must already be interned (waiting on an unknown key
    // fails the task). Re-waiting a satisfied edge is expected and cheap.
    std::function<std::vector<edge>(int step)> edges;

    // Runs once on the worker thread before the step loop (e.g. to wire edges
    // that step 0 must observe). Throw to fail the task. Null = none.
    std::function<void()> on_start;

    // Invoked on the worker thread after the task fails for any reason
    // (on_start, an edge wait, or a step threw), before waiters wake. Must not
    // throw. Null = none.
    std::function<void()> on_failed;
  };

  // Scheduling-event callbacks (tracing). Fire on worker/caller threads with no
  // engine locks held; any null member is skipped.
  struct observer {
    std::function<void(std::string const &key, int target)> thread_start;
    std::function<void(std::string const &key, int completed)> thread_complete;
    std::function<
        void(std::string const &key, int step, std::string const &dep, int watermark)>
        blocked;
    std::function<void(std::string const &key, int step, std::string const &dep)>
        unblocked;
    std::function<void(std::string const &key, int old_target, int new_target)>
        target_extended;
  };

  explicit task_engine(observer obs = {});
  ~task_engine();  // fail_all + join_all

  // Intern a task. Returns true if created; false if the key already exists
  // (cfg is discarded — collisions are the caller's problem to detect).
  bool ensure_task(task_config cfg);
  bool contains(std::string const &key) const;

  // Start the task's worker (idempotent) and ratchet its target to at least
  // `target`. `before_spawn` runs exactly once, only on the call that creates
  // the thread, before the thread exists (for state the worker must observe).
  // If before_spawn throws, the task is failed (waiters see the error; the
  // worker-side on_failed hook does NOT fire) and the exception rethrows.
  // Returns true if this call created the thread.
  bool start_task(std::string const &key,
                  int target,
                  std::function<void()> const &before_spawn = {});

  // Ratchet a task's target watermark upward (no-op if already higher).
  // Targets clamp to step_count — beyond-done is unsatisfiable.
  void extend_target(std::string const &key, int target);
  void extend_to_done(std::string const &key);
  void extend_all_to_done();

  // Block until `key` has completed at least `watermark` steps (clamped to
  // step_count, so an oversized watermark waits for done rather than hanging).
  // Throws std::runtime_error with the task's stored message if it failed.
  void wait_at(std::string const &key, int watermark);

  int completed(std::string const &key) const;
  int target(std::string const &key) const;
  int step_count(std::string const &key) const;
  bool failed(std::string const &key) const;

  // Mark every task failed and wake all waiters (teardown tripwire).
  void fail_all();

  // Join all workers; tolerates tasks created while joining (rescans until a
  // pass finds nothing new — terminates because only live workers create tasks).
  void join_all();

  // (key, message) for every failed task; message may be empty. Call after
  // join_all for a stable view.
  std::vector<std::pair<std::string, std::string>> collect_failures() const;

  // Shared global condition for domain-level rendezvous (e.g. counters
  // decremented from step callbacks). Every step completion, task failure, and
  // fail_all also broadcasts it. `pred` is evaluated under the engine mutex and
  // must not block or call back into this engine.
  void notify_global();
  void wait_global(std::function<bool()> const &pred);

 private:
  struct task {
    task_config cfg;
    std::thread worker;          // guarded by mutex: assigned by start_task, moved out
                                 // by join_all — they race when workers spawn tasks
    std::mutex mutex;            // guards worker, spawn_settled, error; pairs with cv
    std::condition_variable cv;  // target extension / failure wakeups
    std::atomic<int> completed{ 0 };
    std::atomic<int> target{ 0 };
    std::atomic_bool failed{ false };
    std::atomic_bool started{ false };
    bool spawn_settled{ false };  // start_task finished: worker assigned or
                                  // spawn aborted (guarded by mutex)
    std::string error;            // valid when failed (guarded by mutex)
  };

  task *find(std::string const &key) const;  // throws on unknown key
  void ratchet_target(task &t, int target, bool notify_observer = true);
  static std::string current_exception_message();  // call from a catch block
  void fail_task(task *t, std::string error_msg);
  void run_worker(task *t);
  void notify_all_global_locked();

  observer observer_;
  std::unordered_map<std::string, std::unique_ptr<task>> tasks_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace envy
