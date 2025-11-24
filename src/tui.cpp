#include "tui.h"

#include "platform.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

using envy::tui::level;

constexpr std::size_t kSeverityLabelWidth{ 3 };
constexpr std::chrono::milliseconds kRefreshIntervalMs{ 16 };

struct log_event {
  std::chrono::system_clock::time_point timestamp;
  envy::tui::level severity;
  std::string message;
};

using log_entry = std::variant<log_event, envy::trace_event_t>;

struct tui {
  std::queue<log_entry> messages;
  std::function<void(std::string_view)> output_handler;
  std::thread worker;
  std::mutex mutex;         // protects messages queue and cv
  std::mutex stdout_mutex;  // protects raw stdout writes in print_stdout()
  std::condition_variable cv;
  std::atomic_bool stop_requested{ false };
  std::optional<envy::tui::level> level_threshold;
  bool decorated{ false };
  bool initialized{ false };
  bool trace_stderr{ false };
  std::FILE *trace_file{ nullptr };
} s_tui{};

bool envy::tui::g_trace_enabled{ false };

namespace {

std::string_view level_to_string(level value) {
  switch (value) {
    case level::TUI_TRACE: return "TRC";
    case level::TUI_DEBUG: return "DBG";
    case level::TUI_INFO: return "INF";
    case level::TUI_WARN: return "WRN";
    case level::TUI_ERROR: return "ERR";
  }
  return "UNKNOWN";
}

std::tm make_local_tm(std::time_t time) {
  std::tm result{};

#if defined(_WIN32)
  localtime_s(&result, &time);
#else
  localtime_r(&time, &result);
#endif

  return result;
}

std::string format_prefix(level severity) {
  if (!s_tui.decorated) { return {}; }

  auto const now{ std::chrono::system_clock::now() };
  auto const seconds{ std::chrono::time_point_cast<std::chrono::seconds>(now) };
  auto const millis{
    std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count()
  };

  std::time_t const timestamp{ std::chrono::system_clock::to_time_t(now) };
  std::tm const local_tm{ make_local_tm(timestamp) };

  char timestamp_buf[32]{};
  if (std::strftime(timestamp_buf, sizeof timestamp_buf, "%Y-%m-%d %H:%M:%S", &local_tm) ==
      0) {
    return {};
  }

  char prefix_buf[96]{};
  int const written_prefix{ std::snprintf(prefix_buf,
                                          sizeof prefix_buf,
                                          "[%s.%03lld] [%-*s] ",
                                          timestamp_buf,
                                          static_cast<long long>(millis),
                                          static_cast<int>(kSeverityLabelWidth),
                                          level_to_string(severity).data()) };

  if (written_prefix <= 0) { return {}; }

  return std::string{ prefix_buf, static_cast<std::size_t>(written_prefix) };
}

void flush_messages(std::queue<log_entry> &pending,
                    std::function<void(std::string_view)> const &handler) {
  bool wrote_to_stderr{ false };

  while (!pending.empty()) {
    auto entry{ std::move(pending.front()) };
    pending.pop();

    if (auto *log_ptr{ std::get_if<log_event>(&entry) }) {
      std::string output;
      if (s_tui.decorated) {
        auto const prefix{ format_prefix(log_ptr->severity) };
        output.reserve(prefix.size() + log_ptr->message.size() + 1);
        output.append(prefix);
      } else {
        output.reserve(log_ptr->message.size() + 1);
      }
      output.append(log_ptr->message);
      output.push_back('\n');

      if (handler) {
        handler(output);
      } else {
        if (!output.empty()) { std::fwrite(output.data(), 1, output.size(), stderr); }
        wrote_to_stderr = true;
      }
    } else if (auto *trace_ptr{ std::get_if<envy::trace_event_t>(&entry) }) {
      if (s_tui.trace_stderr) {
        std::string output;
        if (s_tui.decorated) {
          auto const prefix{ format_prefix(level::TUI_TRACE) };
          auto const trace_str{ envy::trace_event_to_string(*trace_ptr) };
          output.reserve(prefix.size() + trace_str.size() + 1);
          output.append(prefix);
          output.append(trace_str);
        } else {
          output = envy::trace_event_to_string(*trace_ptr);
        }
        output.push_back('\n');

        if (handler) {
          handler(output);
        } else {
          std::fwrite(output.data(), 1, output.size(), stderr);
          wrote_to_stderr = true;
        }
      }

      if (s_tui.trace_file) {
        auto const json{ envy::trace_event_to_json(*trace_ptr) + "\n" };
        if (std::fwrite(json.data(), 1, json.size(), s_tui.trace_file) != json.size() ||
            std::fflush(s_tui.trace_file) != 0) {
          std::fflush(stderr);
          std::fprintf(stderr, "Fatal: failed to write trace file\n");
          std::fflush(stderr);
          std::abort();
        }
      }
    }
  }

  if (!handler && wrote_to_stderr) { std::fflush(stderr); }
}

void worker_thread() {
  std::unique_lock<std::mutex> lock{ s_tui.mutex };

  while (!s_tui.stop_requested) {
    std::queue<log_entry> pending;
    pending.swap(s_tui.messages);

    lock.unlock();
    flush_messages(pending, s_tui.output_handler);
    lock.lock();

    s_tui.cv.wait_until(lock, std::chrono::steady_clock::now() + kRefreshIntervalMs, [] {
      return s_tui.stop_requested.load();
    });
  }

  std::queue<log_entry> pending;
  pending.swap(s_tui.messages);

  lock.unlock();
  flush_messages(pending, s_tui.output_handler);
}

void log_formatted(level severity, char const *fmt, va_list args) {
  if (!s_tui.initialized || fmt == nullptr) { return; }
  if (s_tui.level_threshold && severity < *s_tui.level_threshold) { return; }

  std::string buffer(1024, '\0');

  va_list args_copy;
  va_copy(args_copy, args);
  int written{ std::vsnprintf(buffer.data(), buffer.size(), fmt, args) };
  if (written <= 0) {
    va_end(args_copy);
    return;
  }

  if (static_cast<std::size_t>(written) >= buffer.size()) {
    buffer.resize(static_cast<std::size_t>(written) + 1);
    written = std::vsnprintf(buffer.data(), buffer.size(), fmt, args_copy);
  }
  va_end(args_copy);

  if (written <= 0) { return; }

  buffer.resize(static_cast<std::size_t>(written));
  if (buffer.empty()) { return; }

  log_event ev{ .timestamp = std::chrono::system_clock::now(),
                .severity = severity,
                .message = std::move(buffer) };

  {
    std::lock_guard<std::mutex> lock{ s_tui.mutex };
    s_tui.messages.push(log_entry{ std::move(ev) });
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
  s_tui.decorated = false;
  s_tui.initialized = true;
  g_trace_enabled = false;
}

void configure_trace_outputs(std::vector<trace_output_spec> outputs) {
  if (!s_tui.initialized) {
    throw std::logic_error{ "envy::tui::configure_trace_outputs called before init" };
  }

  if (s_tui.worker.joinable()) {
    throw std::logic_error{ "envy::tui::configure_trace_outputs called while running" };
  }

  // Close previous file if any
  if (s_tui.trace_file) {
    std::fclose(s_tui.trace_file);
    s_tui.trace_file = nullptr;
  }

  s_tui.trace_stderr = false;

  for (auto const &spec : outputs) {
    if (spec.type == trace_output_type::stderr) {
      s_tui.trace_stderr = true;
    } else if (spec.type == trace_output_type::file && spec.file_path) {
      if (s_tui.trace_file) {
        throw std::logic_error{ "Only one trace file output supported" };
      }
      s_tui.trace_file = std::fopen(spec.file_path->string().c_str(), "w");
      if (!s_tui.trace_file) {
        throw std::runtime_error("Failed to open trace file: " + spec.file_path->string());
      }
    }
  }

  g_trace_enabled = s_tui.trace_stderr || s_tui.trace_file;
}

void run(std::optional<level> threshold, bool decorated_logging) {
  if (!s_tui.initialized) {
    throw std::logic_error{ "envy::tui::run called before init" };
  }

  if (s_tui.worker.joinable()) {
    throw std::logic_error{ "envy::tui::run called while already running" };
  }

  s_tui.level_threshold = std::move(threshold);
  s_tui.decorated = decorated_logging;
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
  g_trace_enabled = false;
  if (s_tui.trace_file) {
    std::fclose(s_tui.trace_file);
    s_tui.trace_file = nullptr;
  }
}

bool is_tty() { return platform::is_tty(); }

void trace(trace_event_t event) {
  if (!g_trace_enabled) { return; }
  {
    std::lock_guard<std::mutex> lock{ s_tui.mutex };
    s_tui.messages.push(log_entry{ std::move(event) });
  }
  s_tui.cv.notify_one();
}

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

  std::lock_guard<std::mutex> lock{ s_tui.stdout_mutex };

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

scope::scope(std::optional<level> threshold, bool decorated_logging) {
  if (!s_tui.initialized) { return; }
  run(std::move(threshold), decorated_logging);
  active = true;
}

scope::~scope() {
  if (active) { shutdown(); }
}

}  // namespace envy::tui
