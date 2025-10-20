#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "tui.h"

#include <string_view>

int main(int argc, char **argv) {
  doctest::Context context;
  context.applyCommandLine(argc, argv);

  envy::tui::init();
  envy::tui::set_output_handler([](std::string_view) {});

  return context.run();
}
