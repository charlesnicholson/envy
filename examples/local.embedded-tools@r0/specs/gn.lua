-- @envy schema "1"
IDENTITY = "local.gn@r0"

VALIDATE = function(opts)
  if opts.ref == nil then
    return "'ref' is a required option (GN doesn't tag, so use a git committish)"
  end
end

FETCH = function(tmp_dir, opts)
  local platform = ({
    darwin = "mac-arm64",
    linux = (envy.ARCH == "x86_64") and "linux-amd64" or "linux-arm64",
    windows = "windows-amd64",
  })[envy.PLATFORM]
  assert(platform, "unsupported platform: " .. envy.PLATFORM)

  return {
    source = "https://chrome-infra-packages.appspot.com/dl/gn/gn/" ..
        platform .. "/+/git_revision:" .. opts.ref,
    dest = "gn.zip",
  }
end

PRODUCTS = { gn = "gn" .. envy.EXE_EXT }
