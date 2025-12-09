identity = "local.uv@r0"

fetch = function(ctx, opts)
  local uri_prefix = "https://github.com/astral-sh/uv/releases/download/"
  local filename = ({
    windows = "x86_64-pc-windows-msvc.zip",
    linux = "x86_64-unknown-linux-musl.tar.gz",
    darwin = "aarch64-apple-darwin.tar.gz",
  })[ENVY_PLATFORM]

  return uri_prefix .. opts.version .. "/uv-" .. filename
end

stage = { strip = (ENVY_PLATFORM == "windows") and 0 or 1 }

local ext = (ENVY_PLATFORM == "windows") and ".exe" or ""
products = { uv = "uv" .. ext }
