#include "tui.h"

#include "platform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

using envy::tui::level;

constexpr std::size_t kSeverityLabelWidth{ 3 };
constexpr std::chrono::milliseconds kRefreshIntervalMs{ 33 };  // 30fps

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

struct section_state {
  unsigned handle;
  envy::tui::section_frame cached_frame;
  bool active;
  bool has_content{ false };  // True after first set_content call

  std::string last_fallback_output;
  std::chrono::steady_clock::time_point last_fallback_print_time;
};

struct tui_progress_state {
  std::vector<section_state> sections;
  unsigned next_handle{ 1 };
  int last_line_count{ 0 };
  std::size_t max_label_width{ 0 };
  std::mutex interactive_mutex;
  bool enabled{ true };
} s_progress{};

bool envy::tui::g_trace_enabled{ false };

#ifdef ENVY_UNIT_TEST
namespace envy::tui::test {
int g_terminal_width{ 0 };
bool g_isatty{ true };
std::chrono::steady_clock::time_point g_now{};
}  // namespace envy::tui::test
#endif

namespace {

int get_terminal_width() {
#ifdef ENVY_UNIT_TEST
  if (envy::tui::test::g_terminal_width > 0) { return envy::tui::test::g_terminal_width; }
#endif

#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi)) {
    return csbi.dwSize.X;
  }
  return 80;
#else
  struct winsize ws;
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) { return ws.ws_col; }
  return 80;
#endif
}

bool is_ansi_supported() {
#ifdef ENVY_UNIT_TEST
  return envy::tui::test::g_isatty;
#endif

  if (!envy::platform::is_tty()) { return false; }

#ifdef _WIN32
  HANDLE const h{ GetStdHandle(STD_ERROR_HANDLE) };
  DWORD mode{ 0 };
  if (GetConsoleMode(h, &mode)) {
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(h, mode)) { return true; }
  }
  return false;
#else
  char const *const term{ std::getenv("TERM") };
  return term && std::strcmp(term, "dumb") != 0;
#endif
}

std::chrono::steady_clock::time_point get_now() {
#ifdef ENVY_UNIT_TEST
  if (envy::tui::test::g_now.time_since_epoch().count() > 0) {
    return envy::tui::test::g_now;
  }
#endif
  return std::chrono::steady_clock::now();
}

constexpr char const *kSpinnerFrames[]{ "|", "/", "-", "\\" };

std::string render_progress_bar(envy::tui::progress_data const &data,
                                std::string_view label,
                                std::size_t max_label_width,
                                int width) {
  constexpr int kBarChars{ 20 };
  int const filled{ static_cast<int>((data.percent / 100.0) * kBarChars) };

  std::string output;
  output += label;
  if (label.size() < max_label_width) {
    output.append(max_label_width - label.size(), ' ');
  }

  // Right-justified percentage (3 chars: "  5%", " 42%", "100%")
  char percent_buf[8]{};
  std::snprintf(percent_buf, sizeof(percent_buf), "%3.0f%%", data.percent);
  output += " ";
  output += percent_buf;

  // Progress bar
  output += " [";
  for (int i{ 0 }; i < kBarChars; ++i) {
    if (i < filled) {
      output += "=";
    } else if (i == filled) {
      output += ">";
    } else {
      output += " ";
    }
  }
  output += "]";

  // Status text (downloaded amount) on the right
  if (!data.status.empty()) {
    // Truncate status if it would wrap the terminal width
    int const base_len{
      static_cast<int>(output.size()) + 1  // pending space before status
    };
    int const available{ width > 0 ? width - base_len : std::numeric_limits<int>::max() };
    std::string status{ data.status };
    if (available > 0 && static_cast<int>(status.size()) > available) {
      if (available > 3) {
        status.resize(static_cast<std::size_t>(available - 3));
        status += "...";
      } else {
        status.resize(static_cast<std::size_t>(available));
      }
    }

    output += " ";
    output += status;
  }

  output += "\n";

  return output;
}

std::string render_text_stream(envy::tui::text_stream_data const &data,
                               std::string_view label,
                               std::size_t max_label_width,
                               int width,
                               std::chrono::steady_clock::time_point now) {
  // Determine which lines to render
  std::size_t start_idx{ 0 };
  std::size_t const num_lines{ data.lines.size() };

  if (data.line_limit > 0 && num_lines > data.line_limit) {
    start_idx = num_lines - data.line_limit;
  }

  // Compute spinner frame
  auto const elapsed{ now - data.start_time };
  auto const elapsed_ms{
    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
  };
  std::size_t const frame_index{ static_cast<std::size_t>((elapsed_ms / 100) % 4) };

  std::string output;
  output += label;
  if (label.size() < max_label_width) {
    output.append(max_label_width - label.size(), ' ');
  }
  output += " ";
  output += kSpinnerFrames[frame_index];
  output += " build output:\n";

  for (std::size_t i = start_idx; i < data.lines.size(); ++i) {
    output += "   ";
    output += data.lines[i];
    output += "\n";
  }

  return output;
}

std::string render_spinner(envy::tui::spinner_data const &data,
                           std::string_view label,
                           std::size_t max_label_width,
                           int width,
                           std::chrono::steady_clock::time_point now) {
  auto const elapsed{ now - data.start_time };
  auto const elapsed_ms{
    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
  };
  std::size_t const frame_index{ static_cast<std::size_t>(
      (elapsed_ms / data.frame_duration.count()) % 4) };

  std::string output;
  output += label;
  if (label.size() < max_label_width) {
    output.append(max_label_width - label.size(), ' ');
  }
  output += " ";
  output += kSpinnerFrames[frame_index];
  output += " ";
  output += data.text;
  output += "\n";

  return output;
}

std::string render_static_text(envy::tui::static_text_data const &data,
                               std::string_view label,
                               std::size_t max_label_width,
                               int width) {
  std::string output;
  output += label;
  if (label.size() < max_label_width) {
    output.append(max_label_width - label.size(), ' ');
  }
  output += " ";
  output += data.text;
  output += "\n";

  return output;
}

std::string render_section_frame_fallback(envy::tui::section_frame const &frame,
                                          std::chrono::steady_clock::time_point now) {
  if (!frame.children.empty()) {
    std::string output;
    auto parent_copy{ frame };
    parent_copy.children.clear();
    if (!parent_copy.phase_label.empty()) {
      parent_copy.label += " (" + parent_copy.phase_label + ")";
      parent_copy.phase_label.clear();
    }
    output += render_section_frame_fallback(parent_copy, now);

    for (auto const &child : frame.children) {
      auto child_copy{ child };
      child_copy.label = "  " + child_copy.label;
      output += render_section_frame_fallback(child_copy, now);
    }
    return output;
  }

  return std::visit(
      [&](auto const &data) -> std::string {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, envy::tui::progress_data>) {
          std::string output{ "[" };
          output += frame.label;
          output += "] ";
          output += data.status;
          output += ": ";
          char buf[16]{};
          std::snprintf(buf, sizeof(buf), "%.1f%%\n", data.percent);
          output += buf;
          return output;
        } else if constexpr (std::is_same_v<T, envy::tui::text_stream_data>) {
          // Determine which lines to render
          std::size_t start_idx{ 0 };
          if (data.line_limit > 0 && data.lines.size() > data.line_limit) {
            start_idx = data.lines.size() - data.line_limit;
          }

          // Compute dots for fallback spinner
          auto const elapsed{ now - data.start_time };
          auto const elapsed_sec{
            std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
          };
          int const dot_count{ static_cast<int>((elapsed_sec % 4) + 1) };  // 1-4 dots

          std::string output{ "[" };
          output += frame.label;
          output += "] ";
          output += std::string(dot_count, '.');
          output += " build output:\n";

          for (std::size_t i{ start_idx }; i < data.lines.size(); ++i) {
            output += "   ";
            output += data.lines[i];
            output += "\n";
          }
          return output;
        } else if constexpr (std::is_same_v<T, envy::tui::spinner_data>) {
          auto const elapsed{ now - data.start_time };
          auto const elapsed_sec{
            std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
          };
          int const dot_count{ static_cast<int>((elapsed_sec % 4) + 1) };

          std::string output{ "[" };
          output += frame.label;
          output += "] ";
          output += data.text;
          output += std::string(dot_count, '.');
          output += "\n";
          return output;
        } else if constexpr (std::is_same_v<T, envy::tui::static_text_data>) {
          std::string output{ "[" };
          output += frame.label;
          output += "] ";
          output += data.text;
          output += "\n";
          return output;
        }
        return "";
      },
      frame.content);
}

std::string render_section_frame(envy::tui::section_frame const &frame,
                                 std::size_t max_label_width,
                                 int width,
                                 bool ansi_mode,
                                 std::chrono::steady_clock::time_point now) {
  if (!ansi_mode) { return render_section_frame_fallback(frame, now); }

  if (!frame.children.empty()) {
    // Parent line (with optional phase suffix), then children indented by two spaces
    std::string output;
    auto parent_copy{ frame };
    parent_copy.children.clear();
    if (!parent_copy.phase_label.empty()) {
      parent_copy.label += " (" + parent_copy.phase_label + ")";
      parent_copy.phase_label.clear();
    }
    output += render_section_frame(parent_copy, max_label_width, width, ansi_mode, now);

    for (auto const &child : frame.children) {
      auto child_copy{ child };
      child_copy.label = "  " + child_copy.label;
      output += render_section_frame(child_copy, max_label_width, width, ansi_mode, now);
    }
    return output;
  }

  return std::visit(
      [&](auto const &data) -> std::string {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, envy::tui::progress_data>) {
          return render_progress_bar(data, frame.label, max_label_width, width);
        } else if constexpr (std::is_same_v<T, envy::tui::text_stream_data>) {
          return render_text_stream(data, frame.label, max_label_width, width, now);
        } else if constexpr (std::is_same_v<T, envy::tui::spinner_data>) {
          return render_spinner(data, frame.label, max_label_width, width, now);
        } else if constexpr (std::is_same_v<T, envy::tui::static_text_data>) {
          return render_static_text(data, frame.label, max_label_width, width);
        }
        return "";
      },
      frame.content);
}

void render_ansi_frame_unlocked(std::vector<section_state> const &sections,
                                std::size_t max_label_width,
                                int last_line_count,
                                int width,
                                std::chrono::steady_clock::time_point now) {
  // Hide cursor
  std::fprintf(stderr, "\x1b[?25l");

  // Render all active sections that have content
  int total_lines{ 0 };
  for (auto const &sec : sections) {
    if (!sec.active || !sec.has_content) { continue; }

    std::string const output{
      render_section_frame(sec.cached_frame, max_label_width, width, true, now)
    };
    std::fprintf(stderr, "%s", output.c_str());

    // Count lines (assumes all outputs end with \n)
    total_lines += static_cast<int>(std::count(output.begin(), output.end(), '\n'));
  }

  // Show cursor
  std::fprintf(stderr, "\x1b[?25h");
  std::fflush(stderr);

  // Update shared state (requires lock)
  {
    std::lock_guard lock{ s_tui.mutex };
    s_progress.last_line_count = total_lines;
  }
}

void render_ansi_frame(int width, std::chrono::steady_clock::time_point now) {
  // Hide cursor
  std::fprintf(stderr, "\x1b[?25l");

  // Clear previous progress region
  if (s_progress.last_line_count > 0) {
    std::fprintf(stderr, "\x1b[%dF\x1b[0J", s_progress.last_line_count);
  }

  // Render all active sections that have content
  int total_lines{ 0 };
  for (auto const &sec : s_progress.sections) {
    if (!sec.active || !sec.has_content) { continue; }

    std::string const output{
      render_section_frame(sec.cached_frame, s_progress.max_label_width, width, true, now)
    };
    std::fprintf(stderr, "%s", output.c_str());

    // Count lines (assumes all outputs end with \n)
    total_lines += static_cast<int>(std::count(output.begin(), output.end(), '\n'));
  }

  s_progress.last_line_count = total_lines;

  // Show cursor
  std::fprintf(stderr, "\x1b[?25h");
  std::fflush(stderr);
}

void render_fallback_frame_unlocked(std::vector<section_state> const &sections,
                                    std::chrono::steady_clock::time_point now) {
  // Throttle: print only if changed and >= 2s elapsed
  constexpr auto kFallbackThrottle{ std::chrono::seconds{ 2 } };

  struct update_info {
    unsigned handle;
    std::string output;
  };
  std::vector<update_info> updates;

  for (auto const &sec : sections) {
    if (!sec.active || !sec.has_content) { continue; }

    std::string const output{ render_section_frame_fallback(sec.cached_frame, now) };

    auto const elapsed{ now - sec.last_fallback_print_time };
    if (output != sec.last_fallback_output && elapsed >= kFallbackThrottle) {
      std::fprintf(stderr, "%s", output.c_str());
      updates.push_back(update_info{ .handle = sec.handle, .output = output });
    }
  }

  std::fflush(stderr);

  // Update shared state (requires lock)
  if (!updates.empty()) {
    std::lock_guard lock{ s_tui.mutex };
    for (auto const &upd : updates) {
      for (auto &sec : s_progress.sections) {
        if (sec.handle == upd.handle) {
          sec.last_fallback_output = upd.output;
          sec.last_fallback_print_time = now;
          break;
        }
      }
    }
  }
}

void render_fallback_frame(std::chrono::steady_clock::time_point now) {
  // Throttle: print only if changed and >= 2s elapsed
  constexpr auto kFallbackThrottle{ std::chrono::seconds{ 2 } };

  for (auto &sec : s_progress.sections) {
    if (!sec.active) { continue; }

    std::string const output{ render_section_frame_fallback(sec.cached_frame, now) };

    auto const elapsed{ now - sec.last_fallback_print_time };
    if (output != sec.last_fallback_output && elapsed >= kFallbackThrottle) {
      std::fprintf(stderr, "%s", output.c_str());
      sec.last_fallback_output = output;
      sec.last_fallback_print_time = now;
    }
  }

  std::fflush(stderr);
}

}  // namespace

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

  std::ostringstream oss;
  oss << '[' << timestamp_buf << '.' << std::setfill('0') << std::setw(3) << millis
      << "] [" << std::left << std::setfill(' ') << std::setw(kSeverityLabelWidth)
      << level_to_string(severity) << "] ";
  return oss.str();
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
    // 1. Flush log queue first
    std::queue<log_entry> pending;
    pending.swap(s_tui.messages);

    // 2. Snapshot progress state (if enabled)
    std::vector<section_state> sections_snapshot;
    std::size_t max_label_width{ 0 };
    int last_line_count{ 0 };

    if (s_progress.enabled) {
      sections_snapshot = s_progress.sections;
      max_label_width = s_progress.max_label_width;
      last_line_count = s_progress.last_line_count;
    }

    // Release lock before any I/O
    lock.unlock();

    // 3. Clear previous progress region (ANSI mode only)
    if (s_progress.enabled) {
      bool const ansi{ is_ansi_supported() };
      if (ansi && last_line_count > 0) {
        std::fprintf(stderr, "\x1b[%dF\x1b[0J", last_line_count);
        std::fflush(stderr);
      }
    }

    // 4. Flush logs (without lock)
    flush_messages(pending, s_tui.output_handler);

    // 5. Render new progress sections (without lock)
    if (s_progress.enabled) {
      int const width{ get_terminal_width() };
      auto const now{ get_now() };
      bool const ansi{ is_ansi_supported() };

      if (ansi) {
        render_ansi_frame_unlocked(sections_snapshot,
                                   max_label_width,
                                   last_line_count,
                                   width,
                                   now);
      } else {
        render_fallback_frame_unlocked(sections_snapshot, now);
      }
    }

    // Reacquire for wait
    lock.lock();
    s_tui.cv.wait_until(lock, std::chrono::steady_clock::now() + kRefreshIntervalMs, [] {
      return s_tui.stop_requested.load();
    });
  }

  // Final flush on shutdown
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
    if (spec.type == trace_output_type::std_err) {
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

void pause_rendering() {
  std::lock_guard lock{ s_tui.mutex };
  if (is_ansi_supported() && s_progress.last_line_count > 0) {
    std::fprintf(stderr, "\x1b[%dF\x1b[0J", s_progress.last_line_count);
    std::fprintf(stderr, "\x1b[?25h");  // Show cursor
    s_progress.last_line_count = 0;
    std::fflush(stderr);
  }
}

void resume_rendering() {
  // No-op: next render cycle will redraw
}

section_handle section_create() {
  if (!s_progress.enabled) { return 0; }  // Skip if disabled

  std::lock_guard lock{ s_tui.mutex };

  unsigned const handle{ s_progress.next_handle++ };
  s_progress.sections.push_back(section_state{ .handle = handle,
                                               .cached_frame = {},
                                               .active = true,
                                               .last_fallback_output = {} });

  return handle;
}

void section_set_content(section_handle h, section_frame const &frame) {
  if (h == 0 || !s_progress.enabled) { return; }  // Skip if disabled or invalid

  std::lock_guard lock{ s_tui.mutex };

  for (auto &sec : s_progress.sections) {
    if (sec.handle == h && sec.active) {
      sec.cached_frame = frame;
      sec.has_content = true;
      s_progress.max_label_width =
          std::max(s_progress.max_label_width, measure_label_width(frame));
      break;
    }
  }
}

void section_release(section_handle h) {
  if (h == 0 || !s_progress.enabled) { return; }  // Skip if disabled or invalid

  std::lock_guard lock{ s_tui.mutex };

  for (auto &sec : s_progress.sections) {
    if (sec.handle == h) {
      sec.active = false;
      break;
    }
  }
}

void acquire_interactive_mode() {
  s_progress.interactive_mutex.lock();
  pause_rendering();
}

void release_interactive_mode() {
  resume_rendering();
  s_progress.interactive_mutex.unlock();
}

interactive_mode_guard::interactive_mode_guard() { acquire_interactive_mode(); }

interactive_mode_guard::~interactive_mode_guard() { release_interactive_mode(); }

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

#ifdef ENVY_UNIT_TEST
namespace test {
std::string render_section_frame(section_frame const &frame) {
  int const width{ g_terminal_width > 0 ? g_terminal_width : 80 };
  auto const now{ g_now.time_since_epoch().count() > 0
                      ? g_now
                      : std::chrono::steady_clock::now() };
  std::size_t const max_width{ measure_label_width(frame) };
  return ::render_section_frame(frame, max_width, width, g_isatty, now);
}
}  // namespace test
#endif

namespace {
std::size_t measure_label_width_impl(section_frame const &frame, std::size_t indent) {
  std::size_t len{ indent + frame.label.size() };
  if (!frame.phase_label.empty()) { len += frame.phase_label.size() + 3; }

  std::size_t max_len{ len };
  for (auto const &child : frame.children) {
    max_len = std::max(max_len, measure_label_width_impl(child, indent + 2));
  }
  return max_len;
}
}  // namespace

std::size_t measure_label_width(section_frame const &frame) {
  return measure_label_width_impl(frame, 0);
}

}  // namespace envy::tui
