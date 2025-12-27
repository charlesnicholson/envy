#pragma once

// Bootstrap: self-deployment of envy binary and Lua type definitions into the cache.
//
// On startup, envy copies itself and its embedded lua-language-server type definitions
// into the cache at <cache>/envy/<version>/. This enables:
// - Shell bootstrap scripts to fetch a known version from the cache
// - IDE autocompletion for envy.lua manifests via lua-language-server
//
// File locking ensures concurrent envy processes don't corrupt the deployment.

#include "cache.h"

#include <filesystem>

namespace envy {

// Deploy the running envy binary and type definitions to the cache.
// Uses file locking for concurrent safety. Called from main() before command dispatch.
// Returns true on success (or if already deployed), false on failure.
bool bootstrap_deploy_envy(cache &c);

// Extract lua-language-server type definitions to the cache.
// Returns the directory containing the type definitions.
std::filesystem::path bootstrap_extract_lua_ls_types();

}  // namespace envy
