-- @envy schema "1"
IDENTITY = "local.taplo@r0"

VALIDATE = function(opts)
  if opts.version == nil then
    return "version option is required"
  end
end

FETCH = function(tmp_dir, opts)
  local arch = (envy.ARCH == "arm64") and "aarch64" or envy.ARCH
  local ext = (envy.PLATFORM == "windows") and ".zip" or ".gz"
  return "https://github.com/tamasfe/taplo/releases/download/" ..
      opts.version .. "/taplo-" .. envy.PLATFORM .. "-" .. arch .. ext
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  local bin = envy.path.join(install_dir, "taplo" .. envy.EXE_EXT)
  if envy.PLATFORM == "windows" then
    envy.copy(envy.path.join(stage_dir, "taplo.exe"), bin)
  else
    local arch = (envy.ARCH == "arm64") and "aarch64" or envy.ARCH
    local gz = envy.path.join(stage_dir, "taplo-" .. envy.PLATFORM .. "-" .. arch .. ".gz")
    envy.run("gzip -dc " .. gz .. " > " .. bin)
    envy.run("chmod +x " .. bin)
  end
end

PRODUCTS = { taplo = "taplo" .. envy.EXE_EXT }
