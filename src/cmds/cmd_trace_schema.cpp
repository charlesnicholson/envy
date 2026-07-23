#include "cmd_trace_schema.h"

#include "trace.h"
#include "tui.h"
#include "util.h"

#include "CLI11.hpp"

#include <string>

namespace envy {

void cmd_trace_schema::register_cli(CLI::App &app, std::function<void(cfg)> on_selected) {
  auto *sub{ app.add_subcommand("trace-schema",
                                "Dump the trace event registry as JSON") };
  sub->callback([on_selected = std::move(on_selected)] { on_selected(cfg{}); });
}

cmd_trace_schema::cmd_trace_schema(
    cfg,
    std::optional<std::filesystem::path> const & /*cli_cache_root*/) {}

void cmd_trace_schema::execute() {
  std::string out{ "{\"schema\":" };
  out.append(std::to_string(kTraceSchemaVersion));
  out.append(",\"events\":{");

  bool first_event{ true };
  for (auto const &event : trace_schema()) {
    if (!first_event) { out.push_back(','); }
    first_event = false;
    out.push_back('"');
    out.append(event.name);
    out.append("\":[");
    bool first_field{ true };
    for (auto const &field : event.fields) {
      if (!first_field) { out.push_back(','); }
      first_field = false;
      out.push_back('"');
      out.append(field);
      out.push_back('"');
    }
    out.push_back(']');
  }

  out.append("}}\n");
  tui::print_stdout("%s", out.c_str());
}

}  // namespace envy
