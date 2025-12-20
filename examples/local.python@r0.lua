IDENTITY = "local.python@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "options must contain 'version', e.g. '3.13.9'"
  end
end

FETCH = function(tmp_dir, opts)
  local uri_prefix =
  "https://github.com/astral-sh/python-build-standalone/releases/download/20251205/"

  local arch = ({ darwin = "aarch64-apple-darwin-pgo+lto",
    linux = "x86_64_v3-unknown-linux-gnu-pgo+lto",
    windows = "x86_64-pc-windows-msvc-pgo" })[envy.PLATFORM]
  assert(arch)

  local suffix = "-full.tar.zst"
  return uri_prefix .. "cpython-" .. opts.version .. "+20251205-" .. arch .. suffix
end

STAGE = { strip = 1 }

PRODUCTS = function(opts)
  local python = (envy.PLATFORM == "windows") and
      "install/python.exe" or "install/bin/python"

  return {
    python3 = opts.provide_python3 and python or nil,
    ["python" .. opts.version:match("^(%d+%.%d+)")] = python
  }
end
