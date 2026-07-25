#pragma once

#include <array>
#include <string>
#include <string_view>

namespace envy {

// Naming and addressing for envy's own release artifacts. Shared by the self-upgrade path
// (which consumes one artifact) and `envy mirror-envy` (which republishes all of them), so
// both agree on what a release is called and where it lives.

// Single source of truth for where envy itself is published. Relocating the project to a
// different GitHub org is a one-line edit here: every derived URL falls out below, and
// both bootstrap scripts are stamped from these constants instead of carrying copies.
#define ENVY_UPSTREAM_REPO_URL "https://github.com/charlesnicholson/envy"

// Default mirror base: release assets hang off this as /v<version>/<archive name>.
inline constexpr std::string_view kEnvyReleaseDownloadUrl{ ENVY_UPSTREAM_REPO_URL
                                                          "/releases/download" };

// Resolves the newest published version via its redirect to the tag. GitHub serves no
// `latest` object, so this is the only way to ask it; a custom mirror answers with a
// `latest` file instead (see kMirrorLatestFile).
inline constexpr std::string_view kEnvyReleaseLatestUrl{ ENVY_UPSTREAM_REPO_URL
                                                        "/releases/latest" };

// Concatenation above happens in the preprocessor; undef so the macro does not leak into
// every translation unit that includes this header.
#undef ENVY_UPSTREAM_REPO_URL

struct envy_release_target {
  std::string_view os;
  std::string_view arch;
};

// Every artifact published per envy release; see .github/workflows/release.yml. Ordered
// for deterministic output when mirroring the whole set.
inline constexpr std::array<envy_release_target, 6> kEnvyReleaseTargets{ {
    { "darwin", "arm64" },
    { "darwin", "x86_64" },
    { "linux", "arm64" },
    { "linux", "x86_64" },
    { "windows", "arm64" },
    { "windows", "x86_64" },
} };

// A version becomes the envy/<version> cache path component, so reject anything that could
// escape it or confuse a shell.
bool envy_release_version_is_valid(std::string_view version);

// Keyed on the target os rather than the host, so a posix host can name (and mirror) the
// windows artifacts.
std::string_view envy_release_archive_ext(std::string_view os);
std::string envy_release_archive_name(std::string_view os, std::string_view arch);

// <mirror_base>/v<version>/<archive name>. mirror_base must not carry a trailing slash:
// for an s3:// mirror the resulting double slash is a distinct, nonexistent key.
std::string envy_release_url(std::string_view mirror_base,
                             std::string_view version,
                             std::string_view os,
                             std::string_view arch);

}  // namespace envy
