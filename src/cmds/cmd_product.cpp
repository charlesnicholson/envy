#include "cmd_product.h"

#include "cache.h"
#include "cmd_common.h"
#include "engine.h"
#include "manifest.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "tui.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace envy {

cmd_product::cmd_product(cfg cfg) : cfg_{ std::move(cfg) } {}

namespace {

void print_products_json(std::vector<product_info> const &products) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i{ 0 }; i < products.size(); ++i) {
    if (i > 0) { oss << ","; }
    oss << "\n  {";
    oss << "\n    \"product\": \"" << products[i].product_name << "\",";
    oss << "\n    \"value\": \"" << products[i].value << "\",";
    oss << "\n    \"provider\": \"" << products[i].provider_canonical << "\",";
    oss << "\n    \"programmatic\": " << (products[i].programmatic ? "true" : "false")
        << ",";
    oss << "\n    \"asset_path\": \""
        << (products[i].programmatic ? "" : products[i].asset_path.string()) << "\"";
    oss << "\n  }";
  }
  if (!products.empty()) { oss << "\n"; }
  oss << "]\n";
  tui::print_stdout("%s", oss.str().c_str());
}

void print_products_aligned(std::vector<product_info> const &products) {
  if (products.empty()) {
    tui::info("No products defined");
    return;
  }

  // Calculate column widths
  size_t max_product{ 0 };
  size_t max_value{ 0 };
  size_t max_provider{ 0 };

  for (auto const &p : products) {
    max_product = std::max(max_product, p.product_name.size());
    max_value = std::max(max_value, p.value.size());
    max_provider = std::max(max_provider, p.provider_canonical.size());
  }

  // Print aligned rows
  for (auto const &p : products) {
    std::string programmatic_marker{ p.programmatic ? " (programmatic)" : "" };
    char buf[4096];
    snprintf(buf,
             sizeof(buf),
             "%-*s  %-*s  %s%s",
             static_cast<int>(max_product),
             p.product_name.c_str(),
             static_cast<int>(max_value),
             p.value.c_str(),
             p.provider_canonical.c_str(),
             programmatic_marker.c_str());
    tui::info("%s", buf);
  }
}

}  // namespace

bool cmd_product::execute() {
  try {
    auto const m{ load_manifest_or_throw(cfg_.manifest_path) };
    auto const cache_root{ resolve_cache_root(cfg_.cache_root) };

    cache c{ cache_root };
    engine eng{ c, m->get_default_shell(nullptr) };

    std::vector<recipe_spec const *> roots;
    roots.reserve(m->packages.size());
    for (auto *pkg : m->packages) { roots.push_back(pkg); }

    eng.run_full(roots);

    // List all products if no product name specified
    if (cfg_.product_name.empty()) {
      auto const products{ eng.collect_all_products() };
      if (cfg_.json) {
        print_products_json(products);
      } else {
        print_products_aligned(products);
      }
      return true;
    }

    // Query single product
    recipe *provider{ eng.find_product_provider(cfg_.product_name) };
    if (!provider) {
      tui::error("Product '%s' has no provider in resolved dependency graph",
                 cfg_.product_name.c_str());
      return false;
    }

    auto const product_it{ provider->products.find(cfg_.product_name) };
    if (product_it == provider->products.end()) {
      tui::error("Product '%s' provider '%s' missing product key (internal error)",
                 cfg_.product_name.c_str(),
                 provider->spec->identity.c_str());
      return false;
    }

    std::string const &value{ product_it->second };
    if (value.empty()) {
      tui::error("product '%s' is empty", cfg_.product_name.c_str());
      return false;
    }

    // Use provider asset_path to construct final output when available
    std::filesystem::path const asset_path{ provider->asset_path };
    bool const programmatic{ provider->result_hash == "programmatic" ||
                             asset_path.empty() };

    if (programmatic) {
      tui::print_stdout("%s\n", value.c_str());
    } else {
      std::filesystem::path full_path{ asset_path / value };
      tui::print_stdout("%s\n", full_path.string().c_str());
    }

    return true;
  } catch (std::exception const &ex) {
    tui::error("product command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
