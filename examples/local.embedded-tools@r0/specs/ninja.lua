-- @envy schema "1"
IDENTITY = "local.ninja@r0"
EXPORTABLE = true

VALIDATE = function(opts)
  if opts.version == nil then
    return "version option is required"
  end
end

FETCH = function(tmp_dir, opts)
  local filename = ({
    darwin = "ninja-mac.zip",
    linux = (envy.ARCH == "x86_64") and "ninja-linux.zip" or "ninja-linux-aarch64.zip",
    windows = "ninja-win.zip",
  })[envy.PLATFORM]
  assert(filename, "unsupported platform: " .. envy.PLATFORM)

  return "https://github.com/ninja-build/ninja/releases/download/" ..
      opts.version .. "/" .. filename
end

PRODUCTS = { ninja = "ninja" .. envy.EXE_EXT }
