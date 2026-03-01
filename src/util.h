#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace envy {

template <typename T, typename... Types>
concept one_of = (std::same_as<T, Types> || ...);

struct uncopyable {
  uncopyable() = default;
  uncopyable(uncopyable &&) = default;
  uncopyable &operator=(uncopyable &&) = default;
};

struct unmovable {
  unmovable() = default;
  unmovable(unmovable const &) = delete;
  unmovable &operator=(unmovable const &) = delete;
};

template <typename... Ts>
struct match : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
match(Ts...) -> match<Ts...>;

// Convert bytes to lowercase hex string
std::string util_bytes_to_hex(void const *data, size_t length);

// Escape a string for JSON output (RFC 8259 compliant).
// Handles \", \\, \b, \f, \n, \r, \t, and \u00xx for other control chars < 0x20.
std::string util_escape_json_string(std::string_view value);

// Convert hex string to bytes (case-insensitive)
std::vector<unsigned char> util_hex_to_bytes(std::string const &hex);

// Convert single hex character to value (0-15). Returns -1 if invalid.
int util_hex_char_to_int(char c);

// RAII file pointer with custom deleter
struct file_deleter {
  void operator()(std::FILE *file) const noexcept;
};
using file_ptr_t = std::unique_ptr<std::FILE, file_deleter>;

// Open file with RAII wrapper. Returns nullptr on failure.
// On Windows, uses _wfopen for proper Unicode path support.
file_ptr_t util_open_file(std::filesystem::path const &path, char const *mode);

// Load entire file into memory as bytes.
// Throws std::runtime_error if file cannot be opened or read.
std::vector<unsigned char> util_load_file(std::filesystem::path const &path);

// Write content to file atomically (temp file + rename).
// Parent directory must exist. Throws std::runtime_error on failure.
void util_write_file(std::filesystem::path const &path, std::string_view content);

// Human-readable byte formatter (B, KB, MB, GB, TB). B uses integer form, higher
// units use one decimal place with rounding (e.g., 1536 -> "1.5KB").
std::string util_format_bytes(std::uint64_t bytes);

// Flatten multi-line script to single line with semicolon delimiters.
// Replaces newlines (\n, \r\n, \r) with "; ", collapses consecutive spaces/tabs to single
// space. Trims trailing semicolons and whitespace. Example: "cmd1\ncmd2\ncmd3" -> "cmd1;
// cmd2; cmd3"
std::string util_flatten_script_with_semicolons(std::string_view script);

// Convert filesystem path to string with trailing separator.
// Ensures Lua expressions like `dir .. "filename"` produce correct paths.
std::string util_path_with_separator(std::filesystem::path const &path);

// Forward-declared from platform.h (can't include here — platform.h includes util.h).
enum class platform_id;

std::vector<platform_id> util_parse_platform_flag(std::string const &value);

// Product mapping: pairs of (product_name, relative_path)
// Example: {"cmake", "bin/cmake.exe"} or {"python", "bin/python3"}
using product_map_t = std::vector<std::pair<std::string, std::string>>;

// Resolve relative path against an anchor directory.
// Throws if `relative` is absolute, or if `anchor` is not an absolute path.
std::filesystem::path util_absolute_path(std::filesystem::path const &relative,
                                         std::filesystem::path const &anchor);

// Simplify cache paths in command string for display.
// First tries to match tokens against product paths (suffix matching).
// Falls back to cache_root prefix detection with filename extraction.
// Example with products: "/cache/.../bin/cmake.exe" -> "cmake" (if products has
// cmake->bin/cmake.exe) Example without: "/cache/assets/pkg/bin/python" -> "python" (if
// starts with cache_root)
std::string util_simplify_cache_paths(std::string_view command,
                                      std::filesystem::path const &cache_root,
                                      product_map_t const &products = {});

// Parsed fields from an exported archive filename stem.
// Stem format: <identity>@<revision>-<platform>-<arch>-blake3-<hash_prefix>
struct parsed_archive_filename {
  std::string identity;     // e.g. "arm.gcc@r2"
  std::string platform;     // e.g. "darwin"
  std::string arch;         // e.g. "arm64"
  std::string hash_prefix;  // e.g. "abcdef0123456789"
};

// Parse exported archive filename stem (without .tar.zst extension).
// Returns nullopt on invalid format.
std::optional<parsed_archive_filename> util_parse_archive_filename(std::string_view stem);

class scoped_path_cleanup : public unmovable {
 public:
  explicit scoped_path_cleanup(std::filesystem::path path);
  ~scoped_path_cleanup();

  void reset(std::filesystem::path path = {});
  std::filesystem::path const &path() const { return path_; }

 private:
  void cleanup();

  std::filesystem::path path_;
};

}  // namespace envy
