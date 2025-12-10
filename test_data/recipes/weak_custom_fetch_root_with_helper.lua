-- Root recipe where the custom fetch dependency has a weak fetch prerequisite,
-- but a strong helper already exists in the graph.
IDENTITY = "local.weak_custom_fetch_root_with_helper@v1"

DEPENDENCIES = {
  { recipe = "local.helper@v1", source = "weak_helper_strong.lua" },
  {
    recipe = "local.custom_fetch_dep@v1",
    source = {
      dependencies = {
        { recipe = "local.helper", weak = { recipe = "local.helper.fallback@v1", source = "weak_helper_fallback.lua" } },
      },
      fetch = function(ctx)
        local path = ctx.tmp_dir .. "/recipe.lua"
        local f, err = io.open(path, "w")
        if not f then
          error("failed to write custom fetch recipe: " .. tostring(err))
        end
        f:write([[
IDENTITY = "local.custom_fetch_dep@v1"

function INSTALL(ctx)
  -- No-op; custom fetch dependency performs work, root install not needed.
end

function CHECK(ctx) return true end
]])
        f:close()
        ctx.commit_fetch("recipe.lua")
      end,
    },
  },
}

function INSTALL(ctx)
  -- No-op; check returns true so install is skipped.
end

function CHECK(ctx)
  return true
end
