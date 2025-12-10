IDENTITY = "local.uv@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option"
  end
end

FETCH = function(ctx, opts)
  local uri_prefix = "https://github.com/astral-sh/uv/releases/download/"
  local filename = ({
    windows = "x86_64-pc-windows-msvc.zip",
    linux = "x86_64-unknown-linux-musl.tar.gz",
    darwin = "aarch64-apple-darwin.tar.gz",
  })[ENVY_PLATFORM]

  return uri_prefix .. opts.version .. "/uv-" .. filename
end

STAGE = { strip = (ENVY_PLATFORM == "windows") and 0 or 1 }

PRODUCTS = { uv = "uv" .. ENVY_EXE_EXT }
