#pragma once

#include <memory>

namespace envy {

class Command;
std::unique_ptr<Command> ParseCommandLine(int argc, char **argv);

}  // namespace envy
