#pragma once

#include "manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace envy {

void reexec_init(char **argv);

// Called by manifest-aware commands after discovering metadata.
// If version mismatch: downloads correct envy to cache, re-execs (never returns).
// Returns normally if: no @envy version, version matches, dev build (0.0.0),
// ENVY_REEXEC set, or ENVY_NO_REEXEC set.
void reexec_if_needed(envy_meta const &meta,
                      std::optional<std::filesystem::path> const &cli_cache_root);

enum class reexec_decision { PROCEED, REEXEC };

reexec_decision reexec_should(std::string_view self_version,
                              std::optional<std::string> const &requested_version,
                              bool reexec_env_set,
                              bool no_reexec_env_set);

std::string reexec_download_url(std::string_view mirror_base,
                                std::string_view version,
                                std::string_view os,
                                std::string_view arch);

}  // namespace envy
