#pragma once

#include "trace.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#if defined(__clang__) || defined(__GNUC__)
#define ENVY_TUI_PRINTF(idx, first) __attribute__((format(printf, idx, first)))
#else
#define ENVY_TUI_PRINTF(idx, first)
#endif

namespace envy::tui {

enum class level { TUI_TRACE, TUI_DEBUG, TUI_INFO, TUI_WARN, TUI_ERROR };

enum class trace_output_type { std_err, file };

struct trace_output_spec {
  trace_output_type type;
  std::optional<std::filesystem::path> file_path;
};

void init();
void configure_trace_outputs(std::vector<trace_output_spec> outputs);
void set_output_handler(std::function<void(std::string_view)> handler);
void run(std::optional<level> threshold = std::nullopt, bool decorated_logging = false);
void shutdown();

extern bool g_trace_enabled;

void trace(trace_event_t event);
void debug(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);
void info(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);
void warn(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);
void error(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);

void print_stdout(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);

bool is_tty();
void pause_rendering();
void resume_rendering();

struct scope {  // raii helper
  explicit scope(std::optional<level> threshold, bool decorated_logging);
  ~scope();

 private:
  bool active{ false };
};

// Section progress API
using section_handle = unsigned;

struct progress_data {
  double percent;
  std::string status;
};

struct text_stream_data {
  std::vector<std::string> lines;
  std::size_t line_limit{ 0 };  // 0 = show all lines, N = show last N lines
  std::chrono::steady_clock::time_point start_time;  // For spinner animation
  std::string header_text;  // Text shown after spinner (e.g., flattened command)
};

struct spinner_data {
  std::string text;
  std::chrono::steady_clock::time_point start_time;
  std::chrono::milliseconds frame_duration{ 100 };
};

struct static_text_data {
  std::string text;
};

struct section_frame {
  std::string label;
  std::variant<progress_data, text_stream_data, spinner_data, static_text_data> content;
  std::vector<section_frame> children;  // Optional grouped children (indented render)
  std::string phase_label;              // Optional phase suffix for grouped parents
};

// Helper for providers/tests that need the rendered label width for alignment.
std::size_t measure_label_width(section_frame const &frame);

section_handle section_create();
void section_set_content(section_handle h, section_frame const &frame);
bool section_has_content(section_handle h);
void section_release(section_handle h);

// Final render - forces one final render cycle of all sections before program exit
void flush_final_render();

// Interactive mode API
void acquire_interactive_mode();
void release_interactive_mode();

class interactive_mode_guard {
 public:
  interactive_mode_guard();
  ~interactive_mode_guard();
};

#ifdef ENVY_UNIT_TEST
namespace test {
extern int g_terminal_width;
extern bool g_isatty;
extern std::chrono::steady_clock::time_point g_now;

std::string render_section_frame(section_frame const &frame);
}  // namespace test
#endif

}  // namespace envy::tui

#undef ENVY_TUI_PRINTF
