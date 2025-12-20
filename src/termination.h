#pragma once

namespace envy {

// Install platform-specific termination handler (SIGINT/SIGTERM on POSIX,
// SetConsoleCtrlHandler on Windows). Handler calls _exit(130) immediately.
void termination_handler_install();

}  // namespace envy
