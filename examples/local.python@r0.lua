identity = "local.python@r0"

local validate = function(opts)
  assert(opts.version, "options must contain version key, e.g. '3.13.9'")
end

fetch = function(ctx, opts)
  validate(opts)
  return {
    "https://github.com/astral-sh/python-build-standalone/releases/download/20251031/cpython-"
    .. opts.version .. "+20251031-aarch64-apple-darwin-pgo+lto-full.tar.zst"
  }
end

stage = { strip = 1 }

products = function(opts)
  validate(opts)
  return {
    python3 = opts.provide_python3 and "install/bin/python" or nil,
    ["python" .. opts.version:match("^(%d+%.%d+)")] = "install/bin/python"
  }
end
