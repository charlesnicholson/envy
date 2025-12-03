#include "product_util.h"

#include "engine.h"
#include "recipe.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace envy {

std::string product_util_resolve(recipe *provider, std::string const &product_name) {
  if (!provider) {
    throw std::runtime_error("Product '" + product_name + "' has no provider");
  }

  auto const it{ provider->products.find(product_name) };
  if (it == provider->products.end()) {
    throw std::runtime_error("Product '" + product_name + "' not found in provider '" +
                             provider->spec->identity + "'");
  }

  std::string const &value{ it->second };
  if (value.empty()) {
    throw std::runtime_error("Product '" + product_name + "' is empty in provider '" +
                             provider->spec->identity + "'");
  }

  if (provider->type == recipe_type::USER_MANAGED) { return value; }

  if (provider->asset_path.empty()) {
    throw std::runtime_error("Product '" + product_name + "' provider '" +
                             provider->spec->identity + "' missing asset path");
  }

  std::filesystem::path const full_path{ provider->asset_path / value };
  return full_path.string();
}

}  // namespace envy
