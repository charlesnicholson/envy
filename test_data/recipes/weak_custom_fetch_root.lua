-- Root recipe with a dependency that uses custom fetch and a weak fetch prerequisite
IDENTITY = "local.weak_custom_fetch_root@v1"

DEPENDENCIES = {
  {
    recipe = "local.custom_fetch_dep@v1",
    source = {
      dependencies = {
        { recipe = "local.helper", weak = { recipe = "local.helper.fallback@v1", source = "weak_helper_fallback.lua" } },
      },
      fetch = function(tmp_dir, options)
        local path = tmp_dir .. "/recipe.lua"
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
        envy.commit_fetch("recipe.lua")
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
