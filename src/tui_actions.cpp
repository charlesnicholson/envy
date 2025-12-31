#include "tui_actions.h"

#include "util.h"

#include <algorithm>
#include <sstream>

namespace envy::tui_actions {

// ==== run_progress ====

run_progress::run_progress(tui::section_handle section,
                           std::string const &pkg_identity,
                           std::filesystem::path const &cache_root,
                           product_map_t products)
    : section_{ section },
      label_{ "[" + pkg_identity + "]" },
      cache_root_{ cache_root },
      products_{ std::move(products) },
      start_time_{ std::chrono::steady_clock::now() },
      lines_{},
      header_text_{} {}

void run_progress::on_command_start(std::string_view cmd) {
  // Flatten and simplify the command for display
  std::string const flattened{ util_flatten_script_with_semicolons(cmd) };
  header_text_ = util_simplify_cache_paths(flattened, cache_root_, products_);

  // Show spinner immediately with the command
  tui::section_set_content(
      section_,
      tui::section_frame{ .label = label_,
                          .content = tui::spinner_data{ .text = header_text_,
                                                        .start_time = start_time_ } });
}

void run_progress::on_output_line(std::string_view line) {
  if (!section_) { return; }

  lines_.emplace_back(line);
  tui::section_set_content(section_,
                           tui::section_frame{ .label = label_,
                                               .content = tui::text_stream_data{
                                                   .lines = lines_,
                                                   .line_limit = 3,
                                                   .start_time = start_time_,
                                                   .header_text = header_text_ } });
}

// ==== extract_progress_tracker ====

extract_progress_tracker::extract_progress_tracker(tui::section_handle section,
                                                   std::string const &pkg_identity,
                                                   std::string const &filename)
    : section_{ section },
      label_{ "[" + pkg_identity + "]" },
      filename_{ filename },
      start_time_{ std::chrono::steady_clock::now() } {
  // Show initial spinner
  tui::section_set_content(
      section_,
      tui::section_frame{ .label = label_,
                          .content = tui::spinner_data{ .text = "extracting " + filename_,
                                                        .start_time = start_time_ } });
}

bool extract_progress_tracker::operator()(extract_progress const &prog) {
  if (!section_) { return true; }

  double percent{ 0.0 };
  if (prog.total_files && *prog.total_files > 0) {
    percent = (prog.files_processed / static_cast<double>(*prog.total_files)) * 100.0;
  } else if (prog.total_bytes && *prog.total_bytes > 0) {
    percent = (prog.bytes_processed / static_cast<double>(*prog.total_bytes)) * 100.0;
  }
  if (percent > 100.0) { percent = 100.0; }

  std::ostringstream status;
  status << prog.files_processed;
  if (prog.total_files) { status << "/" << *prog.total_files; }
  status << " files";
  if (prog.total_bytes) {
    status << " " << util_format_bytes(prog.bytes_processed) << "/"
           << util_format_bytes(*prog.total_bytes);
  } else if (prog.bytes_processed > 0) {
    status << " " << util_format_bytes(prog.bytes_processed);
  }
  status << " " << filename_;

  tui::section_set_content(
      section_,
      tui::section_frame{
          .label = label_,
          .content = tui::progress_data{ .percent = percent, .status = status.str() } });

  return true;
}

// ==== extract_all_progress_tracker ====

extract_all_progress_tracker::extract_all_progress_tracker(
    tui::section_handle section,
    std::string const &pkg_identity,
    std::vector<std::string> const &filenames,
    extract_totals const &totals)
    : section_{ section },
      label_{ "[" + pkg_identity + "]" },
      start_time_{ std::chrono::steady_clock::now() },
      mutex_{},
      children_{},
      grouped_{ filenames.size() > 1 },
      last_prog_{ .bytes_processed = 0,
                  .total_bytes =
                      totals.bytes > 0 ? std::make_optional(totals.bytes) : std::nullopt,
                  .files_processed = 0,
                  .total_files =
                      totals.files > 0 ? std::make_optional(totals.files) : std::nullopt,
                  .current_entry = {},
                  .is_regular_file = false } {
  children_.reserve(filenames.size());
  for (auto const &name : filenames) {
    children_.push_back(
        tui::section_frame{ .label = name,
                            .content = tui::static_text_data{ .text = "pending" } });
  }

  set_frames(last_prog_);
}

void extract_all_progress_tracker::set_frames(extract_progress const &prog) {
  last_prog_ = prog;
  double percent{ 0.0 };
  if (prog.total_files && *prog.total_files > 0) {
    percent = (prog.files_processed / static_cast<double>(*prog.total_files)) * 100.0;
  } else if (prog.total_bytes && *prog.total_bytes > 0) {
    percent = (prog.bytes_processed / static_cast<double>(*prog.total_bytes)) * 100.0;
  }
  if (percent > 100.0) { percent = 100.0; }

  std::ostringstream status;
  status << prog.files_processed;
  if (prog.total_files) { status << "/" << *prog.total_files; }
  status << " files";
  if (prog.total_bytes) {
    status << " " << util_format_bytes(prog.bytes_processed) << "/"
           << util_format_bytes(*prog.total_bytes);
  } else if (prog.bytes_processed > 0) {
    status << " " << util_format_bytes(prog.bytes_processed);
  }

  if (grouped_) {
    tui::section_frame parent{ .label = label_,
                               .content = tui::progress_data{ .percent = percent,
                                                              .status = status.str() },
                               .children = children_,
                               .phase_label = {} };
    tui::section_set_content(section_, parent);
  } else {
    std::string item{ children_.empty() ? "" : children_.front().label };
    tui::section_set_content(
        section_,
        tui::section_frame{
            .label = label_,
            .content = tui::progress_data{
                .percent = percent,
                .status = item.empty() ? status.str() : (status.str() + " " + item) } });
  }
}

auto extract_all_progress_tracker::make_callbacks()
    -> std::pair<extract_progress_cb_t, std::function<void(std::string const &)>> {
  // Progress callback
  auto progress_cb = [this](extract_progress const &prog) -> bool {
    std::lock_guard const lock{ mutex_ };
    set_frames(prog);
    return true;
  };

  // On-file callback (for updating child sections)
  std::optional<std::size_t> last_idx;
  auto on_file_cb = [this, last_idx](std::string const &name) mutable {
    std::lock_guard const lock{ mutex_ };
    if (last_idx && *last_idx < children_.size()) {
      children_[*last_idx].content = tui::static_text_data{ .text = "done" };
    }

    if (auto it{ std::find_if(children_.begin(),
                              children_.end(),
                              [&](auto const &c) { return c.label == name; }) };
        it != children_.end()) {
      auto idx{ static_cast<std::size_t>(std::distance(children_.begin(), it)) };
      last_idx = idx;
      children_[idx].content =
          tui::spinner_data{ .text = "staging",
                             .start_time = std::chrono::steady_clock::now() };
    }
    set_frames(last_prog_);
  };

  return { progress_cb, on_file_cb };
}

// ==== fetch_progress_tracker ====

fetch_progress_tracker::fetch_progress_tracker(tui::section_handle section,
                                               std::string const &pkg_identity,
                                               std::string const &url)
    : section_{ section },
      label_{ "[" + pkg_identity + "]" },
      url_{ url },
      start_time_{ std::chrono::steady_clock::now() } {
  // Show initial spinner
  tui::section_set_content(
      section_,
      tui::section_frame{ .label = label_,
                          .content = tui::spinner_data{ .text = "fetching " + url_,
                                                        .start_time = start_time_ } });
}

bool fetch_progress_tracker::operator()(fetch_progress_t const &prog) {
  if (!section_) { return true; }

  std::visit(
      envy::match{
          [&](fetch_transfer_progress const &p) {
            double percent{ 0.0 };
            if (p.total && *p.total > 0) {
              percent = (p.transferred / static_cast<double>(*p.total)) * 100.0;
            }
            if (percent > 100.0) { percent = 100.0; }

            std::ostringstream status;
            status << util_format_bytes(p.transferred);
            if (p.total) { status << "/" << util_format_bytes(*p.total); }
            status << " " << url_;

            tui::section_set_content(section_,
                                     tui::section_frame{ .label = label_,
                                                         .content = tui::progress_data{
                                                             .percent = percent,
                                                             .status = status.str() } });
          },
          [&](fetch_git_progress const &p) {
            double percent{ 0.0 };
            if (p.total_objects > 0) {
              percent =
                  (p.received_objects / static_cast<double>(p.total_objects)) * 100.0;
            }
            if (percent > 100.0) { percent = 100.0; }

            std::ostringstream status;
            status << p.received_objects;
            if (p.total_objects > 0) { status << "/" << p.total_objects; }
            status << " objects";
            if (p.received_bytes > 0) {
              status << " " << util_format_bytes(p.received_bytes);
            }
            status << " " << url_;

            tui::section_set_content(section_,
                                     tui::section_frame{ .label = label_,
                                                         .content = tui::progress_data{
                                                             .percent = percent,
                                                             .status = status.str() } });
          } },
      prog);

  return true;
}

// ==== fetch_all_progress_tracker ====

fetch_all_progress_tracker::fetch_all_progress_tracker(
    tui::section_handle section,
    std::string const &pkg_identity,
    std::vector<std::string> const &labels)
    : section_{ section },
      label_{ "[" + pkg_identity + "]" },
      mutex_{},
      children_{},
      git_states_(labels.size()),
      grouped_{ labels.size() > 1 } {
  children_.reserve(labels.size());
  for (auto const &label : labels) {
    children_.push_back(tui::section_frame{
        .label = label,
        .content = tui::progress_data{ .percent = 0.0, .status = label } });
  }
}

fetch_progress_cb_t fetch_all_progress_tracker::make_callback(std::size_t slot) {
  return [this, slot](fetch_progress_t const &prog) -> bool {
    std::visit(
        envy::match{ [&](fetch_transfer_progress const &p) { update_transfer(slot, p); },
                     [&](fetch_git_progress const &p) { update_git(slot, p); } },
        prog);
    return true;
  };
}

void fetch_all_progress_tracker::update_transfer(std::size_t slot,
                                                 fetch_transfer_progress const &prog) {
  if (slot >= children_.size()) { return; }

  std::string const &item_label = children_[slot].label;

  if (!prog.total.has_value() || *prog.total == 0) {
    std::ostringstream oss;
    oss << util_format_bytes(prog.transferred);
    if (!grouped_) { oss << " " << item_label; }
    set_frame(slot,
              tui::section_frame{
                  .label = item_label,
                  .content = tui::progress_data{ .percent = 0.0, .status = oss.str() } });
    return;
  }

  double const percent{ (prog.transferred / static_cast<double>(*prog.total)) * 100.0 };
  std::ostringstream oss;
  oss << util_format_bytes(prog.transferred) << "/" << util_format_bytes(*prog.total);
  if (!grouped_) { oss << " " << item_label; }
  set_frame(slot,
            tui::section_frame{ .label = item_label,
                                .content = tui::progress_data{ .percent = percent,
                                                               .status = oss.str() } });
}

void fetch_all_progress_tracker::update_git(std::size_t slot,
                                            fetch_git_progress const &prog) {
  if (slot >= children_.size() || slot >= git_states_.size()) { return; }

  std::uint32_t snapshot_total{ 0 };
  std::uint32_t snapshot_received{ 0 };
  std::uint64_t snapshot_bytes{ 0 };
  double snapshot_percent{ 0.0 };
  std::string child_label;

  {
    std::lock_guard const lock{ mutex_ };
    git_state &state = git_states_[slot];
    state.max_total_objects = std::max(state.max_total_objects, prog.total_objects);
    state.last_received_objects =
        std::max(state.last_received_objects, prog.received_objects);
    state.last_bytes = std::max(state.last_bytes, prog.received_bytes);

    if (state.max_total_objects > 0) {
      double const pct =
          (state.last_received_objects / static_cast<double>(state.max_total_objects)) *
          100.0;
      state.last_percent = std::min(100.0, std::max(pct, state.last_percent));
    }

    snapshot_total = state.max_total_objects;
    snapshot_received = state.last_received_objects;
    snapshot_bytes = state.last_bytes;
    snapshot_percent = state.last_percent;
    child_label = children_[slot].label;
  }

  if (snapshot_total == 0) {
    static auto const epoch{ std::chrono::steady_clock::time_point{} };
    std::string const text{ grouped_ ? "starting..." : "starting... " + child_label };
    set_frame(slot,
              tui::section_frame{
                  .label = child_label,
                  .content = tui::spinner_data{ .text = text, .start_time = epoch } });
    return;
  }

  std::ostringstream oss;
  oss << snapshot_received << "/" << snapshot_total << " objects";
  if (snapshot_bytes > 0) { oss << " " << util_format_bytes(snapshot_bytes); }
  if (!grouped_) { oss << " " << child_label; }

  tui::section_frame child_frame{
    .label = child_label,
    .content = tui::progress_data{ .percent = snapshot_percent, .status = oss.str() }
  };
  if (snapshot_received >= snapshot_total) {
    child_frame.content = tui::static_text_data{ .text = oss.str() };
  }

  set_frame(slot, std::move(child_frame));
}

void fetch_all_progress_tracker::set_frame(std::size_t slot,
                                           tui::section_frame child_frame) {
  std::lock_guard const lock{ mutex_ };

  if (grouped_) {
    if (slot < children_.size()) { children_[slot] = std::move(child_frame); }
    tui::section_frame parent{ .label = label_,
                               .content = tui::static_text_data{ .text = "fetch" },
                               .children = children_,
                               .phase_label = {} };
    tui::section_set_content(section_, parent);
  } else {
    child_frame.label = label_;
    tui::section_set_content(section_, child_frame);
  }
}

}  // namespace envy::tui_actions
