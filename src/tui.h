#pragma once

#include "trace.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#if defined(__clang__) || defined(__GNUC__)
#define ENVY_TUI_PRINTF(idx, first) __attribute__((format(printf, idx, first)))
#else
#define ENVY_TUI_PRINTF(idx, first)
#endif

namespace envy::tui {

enum class level { TUI_TRACE, TUI_DEBUG, TUI_INFO, TUI_WARN, TUI_ERROR };

enum class trace_output_type { stderr, file };

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
  explicit scope(std::optional<level> threshold = std::nullopt,
                 bool decorated_logging = false);
  ~scope();

 private:
  bool active{ false };
};

}  // namespace envy::tui

#undef ENVY_TUI_PRINTF
