-- Root recipe with a dependency that uses custom fetch and a weak fetch prerequisite
identity = "local.weak_custom_fetch_root@v1"

dependencies = {
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
identity = "local.custom_fetch_dep@v1"

function install(ctx)
  -- No-op; custom fetch dependency performs work, root install not needed.
end

function check(ctx) return true end
]])
        f:close()
        ctx.commit_fetch("recipe.lua")
      end,
    },
  },
}

function install(ctx)
  -- No-op; check returns true so install is skipped.
end

function check(ctx)
  return true
end
