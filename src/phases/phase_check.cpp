#include "phase_check.h"

#include "blake3_util.h"
#include "cache.h"
#include "engine.h"
#include "pkg.h"
#include "pkg_cfg.h"
#include "trace.h"
#include "tui.h"
#include "util.h"

#include <chrono>
#include <mutex>
#include <string>
#include <utility>

namespace envy {

namespace {

// Compute hash and perform cache lookup. The Lua state is locked only to read
// PLATFORM/ARCH and released before ensure_pkg: ensure_pkg acquires the cache
// entry's file lock, and holding p->lua across that would invert the lock order
// taken by later phases (cache lock, then p->lua).
cache::ensure_result compute_hash_and_lookup_cache(pkg *p) {
  // Compute hash including resolved weak/ref-only dependencies
  std::string key_for_hash{ p->cfg->format_key() };
  {
    std::lock_guard const deps_lock(p->deps_mutex);
    for (auto const &wk : p->resolved_weak_dependency_keys) { key_for_hash += "|" + wk; }
  }

  auto const digest{ blake3_hash(key_for_hash.data(), key_for_hash.size()) };
  p->canonical_identity_hash = util_bytes_to_hex(digest.data(), 32);
  std::string const hash_prefix{ util_bytes_to_hex(digest.data(), 8) };

  auto const [platform, arch]{ [&] {
    auto const lua_acc{ p->lua.lock() };
    sol::state_view lua{ *lua_acc };
    return std::pair{ lua["envy"]["PLATFORM"].get<std::string>(),
                      lua["envy"]["ARCH"].get<std::string>() };
  }() };

  return p->cache_ptr->ensure_pkg(p->cfg->identity, platform, arch, hash_prefix);
}

}  // namespace

void run_check_phase(pkg *p, engine &eng) {
  phase_trace_scope const phase_scope{ p->cfg->identity,
                                       pkg_phase::pkg_check,
                                       std::chrono::steady_clock::now() };

  // User-managed packages do all their work in check-gated SETUP pairs; the
  // shared cache holds nothing for them, so there is nothing to look up.
  if (p->type != pkg_type::CACHE_MANAGED) { return; }

  std::string const key{ p->cfg->format_key() };

  auto cache_result{ compute_hash_and_lookup_cache(p) };

  if (cache_result.lock) {  // Cache miss
    p->lock = std::move(cache_result.lock);
    tui::debug("phase check: [%s] CACHE MISS - pipeline will execute", key.c_str());
  } else {  // Cache hit - store pkg_path, no lock means subsequent phases skip
    p->pkg_path = cache_result.pkg_path;
    tui::debug("phase check: [%s] CACHE HIT at %s - phases will skip",
               key.c_str(),
               cache_result.pkg_path.string().c_str());
  }
}

}  // namespace envy
