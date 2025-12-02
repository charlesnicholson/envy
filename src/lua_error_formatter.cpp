#include "lua_error_formatter.h"

#include "recipe_spec.h"

#include <sstream>

namespace envy {

// Extract line number from Lua error message (for unit testing)
// Example: "recipe.lua:42: assertion failed" -> 42
std::optional<int> extract_line_number(std::string const &error_msg) {
  // Look for pattern ":line_number:"
  size_t pos{ error_msg.find(".lua:") };
  if (pos == std::string::npos) { return std::nullopt; }

  pos += 5;  // Skip ".lua:"
  size_t const end_pos{ error_msg.find(':', pos) };
  if (end_pos == std::string::npos) { return std::nullopt; }

  try {
    return std::stoi(error_msg.substr(pos, end_pos - pos));
  } catch (...) { return std::nullopt; }
}

// Build provenance chain by walking parent pointers (for unit testing)
std::vector<recipe_spec const *> build_provenance_chain(recipe_spec const *spec) {
  std::vector<recipe_spec const *> chain;
  while (spec) {
    chain.push_back(spec);
    spec = spec->parent;
  }
  return chain;
}

std::string format_lua_error(lua_error_context const &ctx) {
  std::ostringstream oss;

  // Header: identity with options
  oss << "Lua error in " << ctx.r->spec->identity;
  if (!ctx.r->spec->serialized_options.empty() &&
      ctx.r->spec->serialized_options != "{}") {
    oss << ctx.r->spec->serialized_options;
  }
  oss << ":\n  " << ctx.lua_error_message << "\n\n";

  // Recipe file path with line number
  if (ctx.r->recipe_file_path) {
    oss << "Recipe file: " << ctx.r->recipe_file_path->string();
    if (auto line_num = extract_line_number(ctx.lua_error_message)) {
      oss << ":" << *line_num;
    }
    oss << "\n";
  }

  // Declared in (provenance)
  if (!ctx.r->spec->declaring_file_path.empty()) {
    oss << "Declared in: " << ctx.r->spec->declaring_file_path.string() << "\n";
  }

  // Phase
  if (!ctx.phase.empty()) { oss << "Phase: " << ctx.phase << "\n"; }

  // Options
  if (!ctx.r->spec->serialized_options.empty()) {
    oss << "Options: " << ctx.r->spec->serialized_options << "\n";
  }

  // Provenance chain (if nested dependencies)
  auto chain{ build_provenance_chain(ctx.r->spec) };
  if (chain.size() > 1) {
    oss << "\nProvenance chain:\n";
    for (size_t i{ 0 }; i < chain.size(); ++i) {
      oss << "  ";
      for (size_t j{ 0 }; j < i; ++j) { oss << "  "; }
      oss << "<- " << chain[i]->identity;
      if (!chain[i]->declaring_file_path.empty()) {
        oss << " (declared in " << chain[i]->declaring_file_path.filename().string()
            << ")";
      }
      oss << "\n";
    }
  }

  return oss.str();
}

}  // namespace envy
