#pragma once

#include <functional>
#include <optional>
#include <string_view>

#if defined(__clang__) || defined(__GNUC__)
#define ENVY_TUI_PRINTF(idx, first) __attribute__((format(printf, idx, first)))
#else
#define ENVY_TUI_PRINTF(idx, first)
#endif

namespace envy::tui {

enum class level { DEBUG, INFO, WARN, ERROR };

void init();
void set_output_handler(std::function<void(std::string_view)> handler);
void run(std::optional<level> threshold = std::nullopt);
void shutdown();

void debug(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);
void info(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);
void warn(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);
void error(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);

void print_stdout(char const *fmt, ...) ENVY_TUI_PRINTF(1, 2);

bool is_tty();
void pause_rendering();
void resume_rendering();

struct scope {  // raii helper
  explicit scope(std::optional<level> threshold = std::nullopt);
  ~scope();

 private:
  bool active{ false };
};

}  // namespace envy::tui

#undef ENVY_TUI_PRINTF
