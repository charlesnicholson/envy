-- @envy schema "1"
IDENTITY = "local.protobuf@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "'version' is a required option, e.g. '33.3'"
  end
end

FETCH = function(tmp_dir, opts)
  local platform = ({
    darwin = (envy.ARCH == "x86_64") and "osx-x86_64" or "osx-aarch_64",
    linux = (envy.ARCH == "x86_64") and "linux-x86_64" or "linux-aarch_64",
    windows = "win64",
  })[envy.PLATFORM]
  assert(platform, "unsupported platform: " .. envy.PLATFORM)

  local filename = "protoc-" .. opts.version .. "-" .. platform .. ".zip"
  return "https://github.com/protocolbuffers/protobuf/releases/download/v" ..
      opts.version .. "/" .. filename
end

PRODUCTS = { protoc = "bin/protoc" .. envy.EXE_EXT }
