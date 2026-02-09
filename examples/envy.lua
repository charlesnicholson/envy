-- @envy version "0.0.17"
-- @envy bin "bin"
-- @envy deploy "true"
-- @envy root "true"

BUNDLES = {
  ["embedded-tools"] = {
    identity = "local.embedded-tools@r0",
    source = "local.embedded-tools@r0"
  }
}

PACKAGES = {
  { spec = "local.uv@r0", source = "local.uv@r0.lua", options = { version = "0.9.28" } },

  { spec = "local.armgcc@r0", bundle = "embedded-tools",
    options = { version = "14.3.rel1" } },

  { spec = "local.python@r0", source = "local.python@r0.lua",
    options = { version = "3.13.11", provide_python3 = true } },

  { spec = "local.python@r0", source = "local.python@r0.lua",
    options = { version = "3.14.2" } },

  { spec = "local.ninja@r0", bundle = "embedded-tools",
    options = { version = "v1.13.2" } },

  { spec = "local.gn@r0", bundle = "embedded-tools",
    options = { ref = "5964f499767097d81dbe034e8b541c3988168073" } },

  { spec = "local.cmake@r0", source = "local.cmake@r0.lua",
    options = { version = "4.2.3" } },

  { spec = "local.protobuf@r0", source = "local.protobuf@r0.lua",
    options = { version = "33.5" } },

  { spec = "local.swig@r0", source = "local.swig@r0.lua",
    options = { version = "4.4.1" } },

  { spec = "local.clang-tools@r0", source = "local.clang-tools@r0.lua",
    options = { version = "21.1.0", tools = { "clang-format" } } },

  { spec = "local.jlink@r0", source = "local.jlink@r0.lua",
    options = { version = "9.12",
                mode = envy.PLATFORM == "windows" and "install" or "extract" } },

  { spec = "local.taplo@r0", source = "local.taplo@r0.lua",
    options = { version = "0.10.0" } },
}

if envy.PLATFORM ~= "windows" then
  envy.extend(PACKAGES, {
    { spec = "local.ragel@r0", source = "local.ragel@r0.lua" },
  })
end

if envy.PLATFORM == "darwin" then
  envy.extend(PACKAGES, {
    { spec = "local.brew_package@r0", source = "local.brew_package@r0.lua",
      options = { packages = { "ghostty", "neovim", "pv", "bat", "libusb" } } },
  })
end

if envy.PLATFORM == "linux" then
  envy.extend(PACKAGES, {
    { spec = "local.apt@r0", source = "local.apt@r0.lua",
      options = {
        packages = {
          "libglib2.0-0", "libglib2.0-dev", "libudev-dev", "libusb-1.0-0-dev" }
      } } })
end
