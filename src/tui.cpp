#include "tui.h"

#include <queue>
#include <string>

namespace {

class tui {
 public:
  std::queue<std::string> messages;
  std::function<void(std::string_view)> output_handler;
};

static tui s_tui{};

}  // namespace

namespace envy::tui {

void init(std::optional<level> threshold) { (void)threshold; }

void run() {}

void shutdown() {}

bool is_tty() { return false; }

void debug(char const *fmt, ...) { (void)fmt; }

void info(char const *fmt, ...) { (void)fmt; }

void warn(char const *fmt, ...) { (void)fmt; }

void error(char const *fmt, ...) { (void)fmt; }

void print_stdout(char const *fmt, ...) { (void)fmt; }

void pause_rendering() {}

void resume_rendering() {}

void set_output_handler(std::function<void(std::string_view)> handler) { (void)handler; }

}  // namespace envy::tui
