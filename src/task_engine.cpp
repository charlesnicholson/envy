#include "task_engine.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace envy {

task_engine::task_engine(observer obs) : observer_(std::move(obs)) {}

task_engine::~task_engine() {
  fail_all();
  join_all();
}

bool task_engine::ensure_task(task_config cfg) {
  std::lock_guard const lock(mutex_);
  auto const [it, inserted]{ tasks_.try_emplace(cfg.key, nullptr) };
  if (!inserted) { return false; }
  it->second = std::make_unique<task>();
  it->second->cfg = std::move(cfg);
  return true;
}

bool task_engine::contains(std::string const &key) const {
  std::lock_guard const lock(mutex_);
  return tasks_.contains(key);
}

task_engine::task *task_engine::find(std::string const &key) const {
  std::lock_guard const lock(mutex_);
  auto const it{ tasks_.find(key) };
  if (it == tasks_.end()) { throw std::runtime_error("Unknown task: " + key); }
  return it->second.get();
}

void task_engine::ratchet_target(task &t, int target, bool notify_observer) {
  target = std::min(target, t.cfg.step_count);  // beyond-done is unsatisfiable
  int current{ t.target.load() };
  while (current < target) {
    if (t.target.compare_exchange_weak(current, target)) {
      {
        std::lock_guard const lock(t.mutex);
        t.cv.notify_one();
      }
      // Outside the task mutex: observers may reenter the engine, and
      // fail_all takes engine mutex -> task mutex (lock-order inversion).
      if (notify_observer && observer_.target_extended) {
        observer_.target_extended(t.cfg.key, current, target);
      }
      return;
    }
  }
}

bool task_engine::start_task(std::string const &key,
                             int target,
                             std::function<void()> const &before_spawn) {
  task *t{ find(key) };

  bool expected{ false };
  if (t->started.compare_exchange_strong(expected, true)) {
    if (before_spawn) {
      try {
        before_spawn();
      } catch (...) {
        // `started` is latched and no worker will ever exist: fail the task so
        // waiters see an error instead of hanging, then surface to the caller.
        // on_failed is NOT invoked — it is a worker-side hook.
        {
          std::lock_guard const task_lock(t->mutex);
          t->spawn_settled = true;
        }
        fail_task(t, current_exception_message());
        throw;
      }
    }
    // Initial ratchet isn't an extension: thread_start announces the target.
    ratchet_target(*t, target, false);
    if (observer_.thread_start) { observer_.thread_start(key, target); }
    {
      // Under the task mutex: join_all may be reaping concurrently (workers
      // spawn tasks), and the handle hand-off must not tear.
      std::lock_guard const task_lock(t->mutex);
      t->worker = std::thread([this, t] { run_worker(t); });
      t->spawn_settled = true;
    }
    return true;
  }

  ratchet_target(*t, target);  // already running: extend
  return false;
}

void task_engine::extend_target(std::string const &key, int target) {
  ratchet_target(*find(key), target);
}

void task_engine::extend_to_done(std::string const &key) {
  task *t{ find(key) };
  ratchet_target(*t, t->cfg.step_count);
}

void task_engine::extend_all_to_done() {
  std::vector<task *> snapshot;
  {
    std::lock_guard const lock(mutex_);
    snapshot.reserve(tasks_.size());
    for (auto const &[_, t] : tasks_) { snapshot.push_back(t.get()); }
  }
  for (auto *t : snapshot) { ratchet_target(*t, t->cfg.step_count); }
}

void task_engine::wait_at(std::string const &key, int watermark) {
  task *t{ find(key) };
  ratchet_target(*t, watermark);
  watermark = std::min(watermark, t->cfg.step_count);  // "done" is the ceiling

  std::unique_lock lock(mutex_);
  cv_.wait(lock, [t, watermark] { return t->completed >= watermark || t->failed; });

  if (t->failed) {
    lock.unlock();
    std::lock_guard const task_lock(t->mutex);
    throw std::runtime_error(t->error.empty() ? "Task failed: " + key : t->error);
  }
}

int task_engine::completed(std::string const &key) const { return find(key)->completed; }

int task_engine::target(std::string const &key) const { return find(key)->target; }

int task_engine::step_count(std::string const &key) const {
  return find(key)->cfg.step_count;
}

bool task_engine::failed(std::string const &key) const { return find(key)->failed; }

void task_engine::fail_all() {
  std::lock_guard const lock(mutex_);
  for (auto const &[_, t] : tasks_) {
    // Hold the task mutex across the store so a worker can't evaluate its wait
    // predicate false and then sleep through the notify (lost wakeup).
    {
      std::lock_guard const task_lock(t->mutex);
      t->failed = true;
    }
    t->cv.notify_all();
  }
  cv_.notify_all();
}

void task_engine::join_all() {
  // tasks_ can grow while joining: live workers may create and start tasks.
  // Snapshot unjoined started tasks under mutex_ (unique_ptr ownership keeps
  // pointers stable across rehash), reap unlocked, rescan until a pass finds
  // nothing left. Unstarted tasks are excluded: once every started worker is
  // joined, nothing remains that could start one. A started task whose spawn
  // hasn't settled (start_task is between the CAS and the handle assignment)
  // stays in the batch for the next rescan.
  std::unordered_set<task *> joined;
  for (;;) {
    std::vector<task *> batch;
    {
      std::lock_guard const lock(mutex_);
      for (auto const &[_, t] : tasks_) {
        if (t->started && !joined.contains(t.get())) { batch.push_back(t.get()); }
      }
    }
    if (batch.empty()) { return; }
    for (auto *t : batch) {
      std::thread w;
      bool settled{ false };
      {
        std::lock_guard const task_lock(t->mutex);
        w = std::move(t->worker);
        settled = t->spawn_settled;
      }
      if (w.joinable()) {
        w.join();  // outside the task mutex: the worker takes it while running
        joined.insert(t);
      } else if (settled) {
        joined.insert(t);  // spawn aborted (or already reaped): no thread exists
      }
    }
  }
}

std::vector<std::pair<std::string, std::string>> task_engine::collect_failures() const {
  std::vector<std::pair<std::string, std::string>> failures;
  std::lock_guard const lock(mutex_);
  for (auto const &[key, t] : tasks_) {
    if (t->failed) {
      std::lock_guard const task_lock(t->mutex);
      failures.emplace_back(key, t->error);
    }
  }
  return failures;
}

void task_engine::notify_global() { notify_all_global_locked(); }

void task_engine::notify_all_global_locked() {
  std::lock_guard const lock(mutex_);
  cv_.notify_all();
}

void task_engine::wait_global(std::function<bool()> const &pred) {
  std::unique_lock lock(mutex_);
  cv_.wait(lock, pred);
}

void task_engine::run_worker(task *t) {
  std::string const &key{ t->cfg.key };

  try {
    if (t->cfg.on_start) { t->cfg.on_start(); }

    while (t->completed < t->cfg.step_count) {
      if (t->failed) { break; }
      int const done{ t->completed };

      if (done >= t->target) {  // reached target: wait for extension
        {
          std::unique_lock lock(t->mutex);
          t->cv.wait(lock, [t, done] { return t->target > done || t->failed; });
        }
        if (t->failed) { break; }
        // target_extended fires from ratchet_target, where the extension is
        // deterministic; a worker may never park here if the extension lands
        // before its target check.
      }

      int const step{ t->completed };

      if (t->cfg.edges) {
        for (auto const &e : t->cfg.edges(step)) {
          if (observer_.blocked) { observer_.blocked(key, step, e.key, e.watermark); }
          if (e.extend_to > e.watermark) { extend_target(e.key, e.extend_to); }
          wait_at(e.key, e.watermark);
          if (observer_.unblocked) { observer_.unblocked(key, step, e.key); }
        }
      }

      bool const finished_early{ t->cfg.step && t->cfg.step(step) };
      t->completed = finished_early ? t->cfg.step_count : step + 1;
      notify_all_global_locked();  // wake cross-task watermark waiters
    }

    if (observer_.thread_complete) { observer_.thread_complete(key, t->completed); }
  } catch (...) {
    fail_task(t, current_exception_message());
    if (t->cfg.on_failed) { t->cfg.on_failed(); }
    notify_all_global_locked();  // wake waiters again after the hook ran
  }
}

std::string task_engine::current_exception_message() {
  try {
    throw;  // rethrow the in-flight exception to inspect it
  } catch (std::exception const &e) { return e.what(); } catch (...) {
    return "unknown exception";
  }
}

void task_engine::fail_task(task *t, std::string error_msg) {
  {
    std::lock_guard const task_lock(t->mutex);
    t->error = std::move(error_msg);
    t->failed = true;
  }
  t->cv.notify_all();
  notify_all_global_locked();
}

}  // namespace envy
