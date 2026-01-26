#include "libgit2_util.h"

#include <git2.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <stdexcept>
#include <string>
#endif

namespace envy {

#ifndef _WIN32
namespace {

bool g_ssl_certs_configured{ false };

#ifdef __APPLE__
constexpr char const *kCaBundlePath = "/etc/ssl/cert.pem";
#endif

// Configure SSL certificate location for libgit2+mbedTLS.
// mbedTLS doesn't auto-discover system CA stores, so we must set the path explicitly.
// Returns true if CA bundle was found and configured.
bool configure_ssl_certs() {
#ifdef __APPLE__
  struct stat st;
  if (stat(kCaBundlePath, &st) == 0 && S_ISREG(st.st_mode)) {
    git_libgit2_opts(GIT_OPT_SET_SSL_CERT_LOCATIONS, kCaBundlePath, nullptr);
    return true;
  }
  return false;
#else
  static constexpr char const *ca_paths[] = {
    "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu
    "/etc/pki/tls/certs/ca-bundle.crt",    // RHEL/CentOS/Fedora
    "/etc/ssl/ca-bundle.pem",              // OpenSUSE
    "/etc/pki/tls/cacert.pem",             // OpenELEC
  };

  struct stat st;
  for (auto const *path : ca_paths) {
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
      git_libgit2_opts(GIT_OPT_SET_SSL_CERT_LOCATIONS, path, nullptr);
      return true;
    }
  }
  return false;
#endif
}

}  // namespace
#endif

libgit2_scope::libgit2_scope() {
  git_libgit2_init();
#ifndef _WIN32
  g_ssl_certs_configured = configure_ssl_certs();
#endif
}

libgit2_scope::~libgit2_scope() { git_libgit2_shutdown(); }

void libgit2_require_ssl_certs() {
#ifndef _WIN32
  if (!g_ssl_certs_configured) {
#ifdef __APPLE__
    throw std::runtime_error("CA certificate bundle not found at " +
                             std::string(kCaBundlePath));
#else
    throw std::runtime_error(
        "No CA certificate bundle found. Install ca-certificates package.");
#endif
  }
#endif
}

}  // namespace envy
