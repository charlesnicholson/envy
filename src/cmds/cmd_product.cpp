#include "cmd_product.h"

#include "cache.h"
#include "cmd_common.h"
#include "engine.h"
#include "manifest.h"
#include "product_util.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "tui.h"

#include <algorithm>
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
    auto const type_str{ [&]() {
      switch (products[i].type) {
        case recipe_type::CACHE_MANAGED: return "cache-managed";
        case recipe_type::USER_MANAGED: return "user-managed";
        case recipe_type::UNKNOWN: return "unknown";
      }
      return "unknown";
    }() };

    oss << "\n    \"product\": \"" << products[i].product_name << "\",";
    oss << "\n    \"value\": \"" << products[i].value << "\",";
    oss << "\n    \"provider\": \"" << products[i].provider_canonical << "\",";
    oss << "\n    \"type\": \"" << type_str << "\",";
    oss << "\n    \"user_managed\": "
        << (products[i].type == recipe_type::USER_MANAGED ? "true" : "false") << ",";
    oss << "\n    \"asset_path\": \""
        << (products[i].type == recipe_type::USER_MANAGED
                ? ""
                : products[i].asset_path.generic_string())
        << "\"";
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
    std::string const user_managed_marker{ p.type == recipe_type::USER_MANAGED
                                               ? " (user-managed)"
                                               : "" };
    char buf[4096];
    snprintf(buf,
             sizeof(buf),
             "%-*s  %-*s  %s%s",
             static_cast<int>(max_product),
             p.product_name.c_str(),
             static_cast<int>(max_value),
             p.value.c_str(),
             p.provider_canonical.c_str(),
             user_managed_marker.c_str());
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

    // Run recipe-fetch phase only (no downloads/builds)
    eng.resolve_graph(roots);

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

    // Query single product - bring provider to completion
    recipe *provider{ eng.find_product_provider(cfg_.product_name) };
    if (!provider) {
      tui::error("Product '%s' has no provider in resolved dependency graph",
                 cfg_.product_name.c_str());
      return false;
    }

    // Ensure provider recipe reaches completion phase
    eng.ensure_recipe_at_phase(provider->key, recipe_phase::completion);

    std::string const rendered_value{ product_util_resolve(provider, cfg_.product_name) };
    tui::print_stdout("%s\n", rendered_value.c_str());

    return true;
  } catch (std::exception const &ex) {
    tui::error("product command failed: %s", ex.what());
    return false;
  }
}

}  // namespace envy
