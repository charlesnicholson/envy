-- Root spec where the custom fetch dependency has a weak fetch prerequisite,
-- but a strong helper already exists in the graph.
IDENTITY = "local.weak_custom_fetch_root_with_helper@v1"

DEPENDENCIES = {
  { spec = "local.helper@v1", source = "weak_helper_strong.lua" },
  {
    spec = "local.custom_fetch_dep@v1",
    source = {
      dependencies = {
        { spec = "local.helper", weak = { spec = "local.helper.fallback@v1", source = "weak_helper_fallback.lua" } },
      },
      fetch = function(tmp_dir, options)
        local path = tmp_dir .. "/spec.lua"
        local f, err = io.open(path, "w")
        if not f then
          error("failed to write custom fetch recipe: " .. tostring(err))
        end
        f:write([[
IDENTITY = "local.custom_fetch_dep@v1"

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No-op; custom fetch dependency performs work, root install not needed.
end

function CHECK(project_root, options) return true end
]])
        f:close()
        envy.commit_fetch("spec.lua")
      end,
    },
  },
}

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- No-op; check returns true so install is skipped.
end

function CHECK(project_root, options)
  return true
end
