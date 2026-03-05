-- @envy schema "1"
IDENTITY = "local.ninja@r0"
EXPORTABLE = true

OPTIONS = { version = { required = true } }

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
