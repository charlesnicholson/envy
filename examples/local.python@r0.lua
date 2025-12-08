identity = "local.python@r0"

local validate = function(opts)
  assert(opts.version, "options must contain version key, e.g. '3.13.9'")
end

local uri_prefix =
  "https://github.com/astral-sh/python-build-standalone/releases/download/20251205/"

fetch = function(ctx, opts)
  validate(opts)

  local arch = ({ darwin = "aarch64-apple-darwin",
                  linux = "x86_64_v3-unknown-linux-gnu" })[ENVY_PLATFORM]

  local suffix = "-pgo+lto-full.tar.zst"
  return uri_prefix .. "cpython-" .. opts.version .. "+20251205-" .. arch .. suffix
end

stage = { strip = 1 }

products = function(opts)
  validate(opts)
  return {
    python3 = opts.provide_python3 and "install/bin/python" or nil,
    ["python" .. opts.version:match("^(%d+%.%d+)")] = "install/bin/python"
  }
end
