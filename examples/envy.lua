-- @envy version "0.0.0"

PACKAGES = {
  { spec = "local.uv@r0", source = "local.uv@r0.lua",
    options = { version = "0.9.16" } },

  { spec = "local.armgcc@r0", source = "local.armgcc@r0.lua",
    options = { version = "14.3.rel1" } },

  { spec = "local.python@r0", source = "local.python@r0.lua",
    options = { version = "3.13.11", provide_python3 = true } },

  { spec = "local.python@r0", source = "local.python@r0.lua",
    options = { version = "3.14.2" } },

  { spec = "local.ninja@r0", source = "local.ninja@r0.lua",
    options = { version = "v1.13.2" } },

  { spec = "local.gn@r0", source = "local.gn@r0.lua",
    options = { ref = "5964f499767097d81dbe034e8b541c3988168073" }},
}

if envy.PLATFORM == "darwin" then
  envy.extend(PACKAGES, { {
    spec = "local.brew_package@r0", source = "local.brew_package@r0.lua",
    options = { packages = { "ghostty", "neovim", "pv", "bat", "libusb" } }
  } })

end

if envy.PLATFORM == "linux" then
  envy.extend(PACKAGES, { {
    spec = "local.apt@r0", source = "local.apt@r0.lua",
    options = { packages = {
      "libglib2.0-0", "libglib2.0-dev", "libudev-dev", "libusb-1.0-0-dev" } }
} })
end
