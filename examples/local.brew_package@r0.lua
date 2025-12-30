IDENTITY = "local.brew_package@r0"

DEPENDENCIES = { spec = "local.brew@r0", source = "local.brew@r0.lua" }

local missing_packages = {}

CHECK = function(tmp_dir, opts)
  local res = envy.run("brew list", { capture = true, quiet = true })
  if res.exit_code ~= 0 then
    return false
  end

  local installed = {}
  for pkg in res.stdout:gmatch("%S+") do
    installed[pkg] = true
  end

  missing_packages = {}

  for _, pkg in pairs(opts.packages) do
    if not installed[pkg] then
      table.insert(missing_packages, pkg)
    end
  end

  return #missing_packages == 0
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  return "brew install " .. table.concat(missing_packages, " ")
end

