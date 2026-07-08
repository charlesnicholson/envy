#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "tui.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace {

// Terminate the process if any single test case runs longer than this - a hung
// test (deadlock, missed wakeup) must fail the build, not stall it forever.
constexpr std::chrono::seconds kTestTimeout{ 5 };

std::mutex g_watchdog_mutex;
std::string g_current_test;                          // guarded by g_watchdog_mutex
std::chrono::steady_clock::time_point g_test_start;  // guarded by g_watchdog_mutex
std::atomic_bool g_test_running{ false };
std::atomic_bool g_watchdog_shutdown{ false };

struct watchdog_listener : doctest::IReporter {
  explicit watchdog_listener(doctest::ContextOptions const &) {}

  void test_case_start(doctest::TestCaseData const &data) override {
    std::lock_guard const lock(g_watchdog_mutex);
    g_current_test = data.m_name ? data.m_name : "<unnamed>";
    g_test_start = std::chrono::steady_clock::now();
    g_test_running = true;
  }

  void test_case_end(doctest::CurrentTestCaseStats const &) override {
    g_test_running = false;
  }

  void report_query(doctest::QueryData const &) override {}
  void test_run_start() override {}
  void test_run_end(doctest::TestRunStats const &) override {}
  void test_case_reenter(doctest::TestCaseData const &) override {}
  void test_case_exception(doctest::TestCaseException const &) override {}
  void subcase_start(doctest::SubcaseSignature const &) override {}
  void subcase_end() override {}
  void log_assert(doctest::AssertData const &) override {}
  void log_message(doctest::MessageData const &) override {}
  void test_case_skipped(doctest::TestCaseData const &) override {}
};

void watchdog_thread_main() {
  while (!g_watchdog_shutdown) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (!g_test_running) { continue; }
    std::lock_guard const lock(g_watchdog_mutex);
    if (g_test_running && std::chrono::steady_clock::now() - g_test_start > kTestTimeout) {
      std::fprintf(stderr,
                   "\nwatchdog: test case '%s' exceeded %lld seconds; aborting\n",
                   g_current_test.c_str(),
                   static_cast<long long>(kTestTimeout.count()));
      std::fflush(stderr);
      std::abort();
    }
  }
}

}  // namespace

REGISTER_LISTENER("watchdog", 0, watchdog_listener);

int main(int argc, char **argv) {
  doctest::Context context;
  context.applyCommandLine(argc, argv);

  envy::tui::init();
  envy::tui::set_output_handler([](std::string_view) {});
  envy::tui::test::g_isatty = false;

  // The watchdog runs as a background thread. Skip it when requested (set by the
  // valgrind CI step): a running thread makes fork() in the shell tests misbehave
  // under valgrind, and a 5s wall-clock limit is meaningless under its ~30x slowdown.
  bool const watchdog_enabled{ std::getenv("ENVY_TEST_NO_WATCHDOG") == nullptr };

  std::thread watchdog;
  if (watchdog_enabled) { watchdog = std::thread{ watchdog_thread_main }; }
  int const rc{ context.run() };
  if (watchdog.joinable()) {
    g_watchdog_shutdown = true;
    watchdog.join();
  }
  return rc;
}
