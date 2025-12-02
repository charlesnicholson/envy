identity = "local.python@r0"

fetch = function(ctx, opts) return {
  "https://github.com/astral-sh/python-build-standalone/releases/download/20251031/cpython-"
  .. opts.version .. "+20251031-aarch64-apple-darwin-pgo+lto-full.tar.zst"
} end

stage = { strip = 1 }

products = function(opts)
  return {
    python3 = opts.provide_python3 and "install/bin/python" or nil,
    ["python" .. opts.version:match("^(%d+%.%d+)")] = "install/bin/python"
  }
end

