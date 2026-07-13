-- @envy schema "1"
IDENTITY = "local.brew_package@r0"
PLATFORMS = { "darwin" }
USER_MANAGED = true

-- Dependency entries may select the dependency's SETUP pairs.
DEPENDENCIES = {
  { spec = "local.brew@r0", source = "local.brew@r0.lua", setup = { "brew" } },
}

local missing_packages = {}

SETUP = {
  packages = {
    CHECK = function(pkg_dir, opts)
      local res = envy.run("brew list", { capture = true, quiet = true, check = false })
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
    end,

    INSTALL = function(pkg_dir, opts)
      return "brew install " .. table.concat(missing_packages, " ")
    end,
  },
}
