#include "tui.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using envy::tui::level;

constexpr std::size_t kSeverityLabelWidth{ 3 };
constexpr std::chrono::milliseconds kRefreshIntervalMs{ 16 };

struct tui {
  std::queue<std::string> messages;
  std::function<void(std::string_view)> output_handler;
  std::thread worker;
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic_bool stop_requested{ false };
  std::optional<envy::tui::level> level_threshold;
  bool initialized{ false };
} s_tui{};

namespace {

void flush_messages(std::queue<std::string> &pending,
                    std::function<void(std::string_view)> const &handler) {
  bool wrote_to_stderr{ false };

  while (!pending.empty()) {
    auto const &message{ pending.front() };

    if (handler) {
      handler(message);
    } else {
      if (!message.empty()) { std::fwrite(message.data(), 1, message.size(), stderr); }
      wrote_to_stderr = true;
    }

    pending.pop();
  }

  if (!handler && wrote_to_stderr) { std::fflush(stderr); }
}

void worker_thread() {
  std::unique_lock<std::mutex> lock{ s_tui.mutex };

  while (!s_tui.stop_requested) {
    std::queue<std::string> pending;
    pending.swap(s_tui.messages);

    lock.unlock();
    flush_messages(pending, s_tui.output_handler);
    lock.lock();

    s_tui.cv.wait_until(lock, std::chrono::steady_clock::now() + kRefreshIntervalMs, [] {
      return s_tui.stop_requested.load();
    });
  }

  std::queue<std::string> pending;
  pending.swap(s_tui.messages);

  lock.unlock();
  flush_messages(pending, s_tui.output_handler);
}

std::string_view level_to_string(level value) {
  switch (value) {
    case level::TUI_DEBUG: return "DBG";
    case level::TUI_INFO: return "INF";
    case level::TUI_WARN: return "WRN";
    case level::TUI_ERROR: return "ERR";
  }
  return "UNKNOWN";
}

std::tm make_local_tm(std::time_t time) {
#if defined(_WIN32)
  std::tm result{};
  localtime_s(&result, &time);
  return result;
#else
  std::tm result{};
  localtime_r(&time, &result);
  return result;
#endif
}

void log_formatted(level severity, char const *fmt, va_list args) {
  if (!s_tui.initialized || fmt == nullptr) { return; }

  std::optional<level> threshold_snapshot;
  {
    std::lock_guard<std::mutex> lock{ s_tui.mutex };
    threshold_snapshot = s_tui.level_threshold;
    if (threshold_snapshot && severity < *threshold_snapshot) { return; }
  }

  char prefix_buf[96]{};
  std::size_t prefix_len{ 0 };

  if (threshold_snapshot) {
    auto const now{ std::chrono::system_clock::now() };
    auto const seconds{ std::chrono::time_point_cast<std::chrono::seconds>(now) };
    auto const millis{
      std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count()
    };

    std::time_t const timestamp{ std::chrono::system_clock::to_time_t(now) };
    std::tm const local_tm{ make_local_tm(timestamp) };
    char timestamp_buf[32]{};
    if (std::strftime(timestamp_buf,
                      sizeof timestamp_buf,
                      "%Y-%m-%d %H:%M:%S",
                      &local_tm) != 0) {
      auto const label{ level_to_string(severity) };

      int const written_prefix{ std::snprintf(prefix_buf,
                                              sizeof prefix_buf,
                                              "[%s.%03lld] [%-*s] ",
                                              timestamp_buf,
                                              static_cast<long long>(millis),
                                              static_cast<int>(kSeverityLabelWidth),
                                              label.data()) };

      if (written_prefix > 0) {
        prefix_len = static_cast<std::size_t>(written_prefix);
      } else {
        threshold_snapshot.reset();
      }
    } else {
      threshold_snapshot.reset();
    }
  }

  std::string buffer(1024 + prefix_len, '\0');
  std::size_t offset{ 0 };
  if (prefix_len > 0) {
    std::memcpy(buffer.data(), prefix_buf, prefix_len);
    offset = prefix_len;
  }

  va_list args_copy;
  va_copy(args_copy, args);
  int written{ std::vsnprintf(buffer.data() + offset, buffer.size() - offset, fmt, args) };
  if (written <= 0) {
    va_end(args_copy);
    return;
  }

  if (offset + static_cast<std::size_t>(written) >= buffer.size()) {
    buffer.resize(offset + static_cast<std::size_t>(written) + 1);
    written =
        std::vsnprintf(buffer.data() + offset, buffer.size() - offset, fmt, args_copy);
  }
  va_end(args_copy);

  if (written <= 0) { return; }

  buffer.resize(offset + static_cast<std::size_t>(written));
  if (buffer.empty()) { return; }

  // Ensure message ends with newline
  if (buffer.back() != '\n') { buffer.push_back('\n'); }

  {
    std::lock_guard<std::mutex> lock{ s_tui.mutex };
  if (s_tui.level_threshold && severity < *s_tui.level_threshold) { return; }
    s_tui.messages.push(std::move(buffer));
  }

  s_tui.cv.notify_one();
}

}  // namespace

namespace envy::tui {

void init() {
  if (s_tui.initialized) {
    throw std::logic_error{ "envy::tui::init called more than once" };
  }

  s_tui.level_threshold = std::nullopt;
  s_tui.initialized = true;
}

void run(std::optional<level> threshold) {
  if (!s_tui.initialized) {
    throw std::logic_error{ "envy::tui::run called before init" };
  }

  if (s_tui.worker.joinable()) {
    throw std::logic_error{ "envy::tui::run called while already running" };
  }

  s_tui.level_threshold = std::move(threshold);
  s_tui.stop_requested = false;
  s_tui.worker = std::thread{ worker_thread };
}

void shutdown() {
  if (!s_tui.worker.joinable()) {
    throw std::logic_error{ "envy::tui::shutdown called while not running" };
  }

  s_tui.stop_requested = true;
  s_tui.cv.notify_all();
  s_tui.worker.join();
  s_tui.worker = std::thread{};
  s_tui.stop_requested = false;
}

bool is_tty() { return false; }

void debug(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_formatted(level::TUI_DEBUG, fmt, args);
  va_end(args);
}

void info(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_formatted(level::TUI_INFO, fmt, args);
  va_end(args);
}

void warn(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_formatted(level::TUI_WARN, fmt, args);
  va_end(args);
}

void error(char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_formatted(level::TUI_ERROR, fmt, args);
  va_end(args);
}

void print_stdout(char const *fmt, ...) {
  if (!fmt) { return; }
  va_list args;
  va_start(args, fmt);
  int const written{ std::vprintf(fmt, args) };
  va_end(args);
  if (written > 0) { std::fflush(stdout); }
}

void pause_rendering() {}

void resume_rendering() {}

void set_output_handler(std::function<void(std::string_view)> handler) {
  if (!s_tui.initialized) {
    throw std::logic_error{ "envy::tui::set_output_handler called before init" };
  }

  if (s_tui.worker.joinable() || s_tui.stop_requested) {
    throw std::logic_error{ "envy::tui::set_output_handler called while running" };
  }

  std::lock_guard<std::mutex> lock{ s_tui.mutex };
  s_tui.output_handler = std::move(handler);
}

scope::scope(std::optional<level> threshold) {
  if (!s_tui.initialized) { return; }
  run(std::move(threshold));
  active = true;
}

scope::~scope() {
  if (active) { shutdown(); }
}

}  // namespace envy::tui
