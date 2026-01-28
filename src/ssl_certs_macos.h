#pragma once

#include <vector>

namespace envy {

// Extract system trusted CA certificates from macOS Keychain.
// Returns PEM-encoded certificate bundle.
std::vector<unsigned char> extract_system_ca_certs();

}  // namespace envy
