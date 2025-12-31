#include "fetch.h"

#include "aws_util.h"
#include "libcurl_util.h"
#include "util.h"

#include "git2.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace envy {
namespace {

std::filesystem::path prepare_destination(std::filesystem::path destination) {
  if (destination.empty()) {
    throw std::invalid_argument("fetch: destination path is empty");
  }

  if (!destination.is_absolute()) { destination = std::filesystem::absolute(destination); }
  destination = destination.lexically_normal();

  if (auto const parent{ destination.parent_path() }; !parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to create destination parent: " +
                               parent.string() + ": " + ec.message());
    }
  }

  return destination;
}

std::filesystem::path resolve_file_path(
    std::string const &canonical_path,
    std::optional<std::filesystem::path> const &file_root) {
  std::filesystem::path source{ canonical_path };
  return std::filesystem::absolute(source.is_relative() && file_root ? *file_root / source
                                                                     : source)
      .lexically_normal();
}

fetch_result fetch_local_file(std::string const &canonical_path,
                              std::filesystem::path const &destination,
                              std::optional<std::filesystem::path> const &file_root) {
  auto const source{ resolve_file_path(canonical_path, file_root) };

  // Validate source exists
  std::error_code ec;
  if (!std::filesystem::exists(source, ec)) {
    throw std::runtime_error("fetch: source file does not exist: " + source.string());
  }
  if (ec) {
    throw std::runtime_error("fetch: failed to check source: " + source.string() + ": " +
                             ec.message());
  }

  auto const dest{ prepare_destination(destination) };

  bool const is_directory{ std::filesystem::is_directory(source, ec) };
  if (ec) {
    throw std::runtime_error("fetch: failed to check if source is directory: " +
                             source.string() + ": " + ec.message());
  }

  if (is_directory) {
    std::filesystem::copy(source,
                          dest,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to copy directory: " + source.string() +
                               " -> " + dest.string() + ": " + ec.message());
    }
  } else {
    std::filesystem::copy_file(source,
                               dest,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
      throw std::runtime_error("fetch: failed to copy file: " + source.string() + " -> " +
                               dest.string() + ": " + ec.message());
    }
  }

  return fetch_result{ .scheme = uri_scheme::LOCAL_FILE_ABSOLUTE,
                       .resolved_source = source,
                       .resolved_destination = dest };
}

int git_fetch_progress_callback(git_indexer_progress const *stats, void *payload) {
  auto *cb{ static_cast<fetch_progress_cb_t *>(payload) };
  if (!cb || !*cb) { return 0; }

  fetch_git_progress progress{
    .total_objects = stats->total_objects,
    .indexed_objects = stats->indexed_objects,
    .received_objects = stats->received_objects,
    .total_deltas = stats->total_deltas,
    .indexed_deltas = stats->indexed_deltas,
    .received_bytes = stats->received_bytes,
  };

  return (*cb)(progress) ? 0 : -1;
}

// Attempt git clone with optional shallow depth. Returns nullptr on failure (no throw).
git_repository *try_git_clone(std::string const &url,
                              std::filesystem::path const &dest,
                              fetch_progress_cb_t const &progress,
                              int depth) {
  git_clone_options clone_opts;
  git_clone_options_init(&clone_opts, GIT_CLONE_OPTIONS_VERSION);

  if (depth > 0) { clone_opts.fetch_opts.depth = depth; }
  clone_opts.fetch_opts.callbacks.transfer_progress = git_fetch_progress_callback;
  clone_opts.fetch_opts.callbacks.payload = const_cast<fetch_progress_cb_t *>(&progress);

  git_repository *repo_raw{ nullptr };
  if (git_clone(&repo_raw, url.c_str(), dest.string().c_str(), &clone_opts)) {
    return nullptr;
  }
  return repo_raw;
}

// Try to resolve ref in repo. Returns nullptr on failure (no throw).
git_object *try_resolve_ref(git_repository *repo, std::string const &ref) {
  if (git_object *obj{ nullptr }; !git_revparse_single(&obj, repo, ref.c_str())) {
    return obj;
  }
  return nullptr;
}

fetch_result fetch_git_repo(std::string const &url,
                            std::string const &ref,
                            std::filesystem::path const &destination,
                            fetch_progress_cb_t const &progress) {
  auto const dest{ prepare_destination(destination) };

  // Try shallow clone first; fall back to full clone if shallow fails or ref not found.
  // Some servers (e.g., googlesource.com) have libgit2 shallow clone issues.
  // Shallow clones also may not fetch all tags, causing ref resolution to fail.
  git_repository *repo_raw{ try_git_clone(url, dest, progress, 1) };
  git_object *target_obj{ nullptr };
  bool need_full_clone{ !repo_raw };

  if (repo_raw) {
    target_obj = try_resolve_ref(repo_raw, ref);
    if (!target_obj) {
      // Shallow clone succeeded but ref not found - need full clone
      git_repository_free(repo_raw);
      repo_raw = nullptr;
      need_full_clone = true;
    }
  }

  if (need_full_clone) {
    std::error_code ec;
    std::filesystem::remove_all(dest, ec);
    std::filesystem::create_directories(dest, ec);

    repo_raw = try_git_clone(url, dest, progress, 0);
    if (!repo_raw) {
      git_error const *git_err{ git_error_last() };
      std::string msg{ "fetch_git: clone failed: " };
      if (git_err) { msg += git_err->message; }
      throw std::runtime_error(msg);
    }

    target_obj = try_resolve_ref(repo_raw, ref);
    if (!target_obj) {
      git_repository_free(repo_raw);
      git_error const *git_err{ git_error_last() };
      std::string msg{ "fetch_git: failed to resolve ref '" + ref + "': " };
      if (git_err) { msg += git_err->message; }
      throw std::runtime_error(msg);
    }
  }

  std::unique_ptr<git_repository, decltype(&git_repository_free)> repo{
    repo_raw,
    git_repository_free
  };

  std::unique_ptr<git_object, decltype(&git_object_free)> target{ target_obj,
                                                                  git_object_free };

  git_checkout_options checkout_opts;
  git_checkout_options_init(&checkout_opts, GIT_CHECKOUT_OPTIONS_VERSION);
  checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;

  if (git_checkout_tree(repo.get(), target.get(), &checkout_opts)) {
    git_error const *git_err{ git_error_last() };
    std::string msg{ "fetch_git: checkout failed: " };
    if (git_err) { msg += git_err->message; }
    throw std::runtime_error(msg);
  }

  // Update HEAD to point to the target (detached HEAD state)
  git_oid const *target_oid{ git_object_id(target.get()) };
  if (git_repository_set_head_detached(repo.get(), target_oid)) {
    git_error const *git_err{ git_error_last() };
    std::string msg{ "fetch_git: failed to update HEAD: " };
    if (git_err) { msg += git_err->message; }
    throw std::runtime_error(msg);
  }

  return fetch_result{ .scheme = uri_scheme::GIT,
                       .resolved_source = std::filesystem::path{ url },
                       .resolved_destination = dest };
}

}  // namespace

fetch_result fetch_single(fetch_request const &request) {
  // Helper for HTTP/HTTPS/FTP/FTPS requests that all use libcurl
  auto const fetch_via_curl = [](auto const &req) -> fetch_result {
    auto const info{ uri_classify(req.source) };
    if (info.canonical.empty() && info.scheme == uri_scheme::UNKNOWN) {
      throw std::invalid_argument("fetch: source URI is empty");
    }
    return fetch_result{ .scheme = info.scheme,
                         .resolved_source = std::filesystem::path{ info.canonical },
                         .resolved_destination = libcurl_download(info.canonical,
                                                                  req.destination,
                                                                  req.progress) };
  };

  return std::visit(
      match{
          [&](fetch_request_http const &req) { return fetch_via_curl(req); },
          [&](fetch_request_https const &req) { return fetch_via_curl(req); },
          [&](fetch_request_ftp const &req) { return fetch_via_curl(req); },
          [&](fetch_request_ftps const &req) { return fetch_via_curl(req); },
          [](fetch_request_s3 const &req) -> fetch_result {
            auto const info{ uri_classify(req.source) };
            if (info.canonical.empty() && info.scheme == uri_scheme::UNKNOWN) {
              throw std::invalid_argument("fetch: source URI is empty");
            }
            return fetch_result{ .scheme = info.scheme,
                                 .resolved_source =
                                     std::filesystem::path{ info.canonical },
                                 .resolved_destination = aws_s3_download(
                                     s3_download_request{ .uri = info.canonical,
                                                          .destination = req.destination,
                                                          .region = req.region,
                                                          .progress = req.progress }) };
          },
          [](fetch_request_file const &req) -> fetch_result {
            auto const info{ uri_classify(req.source) };
            if (info.canonical.empty() && info.scheme == uri_scheme::UNKNOWN) {
              throw std::invalid_argument("fetch: source URI is empty");
            }
            return fetch_local_file(info.canonical, req.destination, req.file_root);
          },
          [](fetch_request_git const &req) -> fetch_result {
            return fetch_git_repo(req.source, req.ref, req.destination, req.progress);
          },
      },
      request);
}

std::vector<fetch_result_t> fetch(std::vector<fetch_request> const &requests) {
  std::vector<fetch_result_t> results(requests.size());

  std::vector<std::thread> workers;
  workers.reserve(requests.size());

  for (size_t i = 0; i < requests.size(); ++i) {
    workers.emplace_back([i, &requests, &results]() {
      try {
        results[i] = fetch_single(requests[i]);
      } catch (std::exception const &e) {
        results[i] = std::string(e.what());
      } catch (...) { results[i] = "Unknown error during fetch"; }
    });
  }

  for (auto &t : workers) { t.join(); }

  return results;
}

}  // namespace envy
