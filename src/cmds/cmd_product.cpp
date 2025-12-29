#include "cmd_product.h"

#include "cache.h"
#include "cmd_common.h"
#include "engine.h"
#include "manifest.h"
#include "product_util.h"
#include "recipe.h"
#include "recipe_spec.h"
#include "tui.h"

#include "CLI11.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace envy {

void cmd_product::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand(
      "product",
      "Query product value or list all products from manifest") };
  auto *cfg_ptr{ new cfg{} };
  sub->add_option("product", cfg_ptr->product_name, "Product name (omit to list all)");
  sub->add_option("--manifest", cfg_ptr->manifest_path, "Path to envy.lua manifest");
  sub->add_flag("--json", cfg_ptr->json, "Output as JSON (to stdout)");
  sub->callback(
      [cfg_ptr, on_selected = std::move(on_selected)] { on_selected(*cfg_ptr); });
}

cmd_product::cmd_product(cfg cfg,
                         std::optional<std::filesystem::path> const &cli_cache_root)
    : cfg_{ std::move(cfg) }, cli_cache_root_{ cli_cache_root } {}

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
    std::ostringstream oss;
    oss << std::left << std::setw(max_product) << p.product_name << "  "
        << std::setw(max_value) << p.value << "  " << p.provider_canonical
        << user_managed_marker;
    tui::info("%s", oss.str().c_str());
  }
}

}  // namespace

void cmd_product::execute() {
  auto const m{ load_manifest_or_throw(cfg_.manifest_path) };
  auto c{ cache::ensure(cli_cache_root_, m->meta.cache) };
  engine eng{ *c, m->get_default_shell(nullptr) };

  std::vector<recipe_spec const *> roots;
  roots.reserve(m->packages.size());
  for (auto *pkg : m->packages) { roots.push_back(pkg); }

  eng.resolve_graph(roots);

  if (cfg_.product_name.empty()) {
    auto const products{ eng.collect_all_products() };
    if (cfg_.json) {
      print_products_json(products);
    } else {
      print_products_aligned(products);
    }
    return;
  }

  recipe *provider{ eng.find_product_provider(cfg_.product_name) };
  if (!provider) {
    throw std::runtime_error("product: '" + cfg_.product_name +
                             "' has no provider in resolved dependency graph");
  }

  eng.extend_dependencies_to_completion(provider);
  eng.ensure_recipe_at_phase(provider->key, recipe_phase::completion);

  std::string const rendered_value{ product_util_resolve(provider, cfg_.product_name) };
  tui::print_stdout("%s\n", rendered_value.c_str());
}

}  // namespace envy
