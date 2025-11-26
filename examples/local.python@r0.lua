identity = "local.python@r0"

fetch = "https://github.com/astral-sh/python-build-standalone/releases/download/20251031/cpython-3.13.9+20251031-aarch64-apple-darwin-pgo+lto-full.tar.zst"

stage = { strip = 1 }

products = function(ctx) return {
  python3 = "install/bin/python",
  ["python"] = ctx.options.version .. "install/bin/python"
} end
