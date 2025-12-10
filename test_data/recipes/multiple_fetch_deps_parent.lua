IDENTITY = "local.multiple_fetch_deps_parent@v1"

DEPENDENCIES = {
  {
    recipe = "local.multiple_fetch_deps_child@v1",
    source = {
      dependencies = {
        { recipe = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" },
        { recipe = "local.fetch_dep_helper@v1", source = "fetch_dep_helper.lua" }
      },
      fetch = function(ctx, opts)
        local recipe_content = [[
IDENTITY = "local.multiple_fetch_deps_child@v1"
DEPENDENCIES = {}
function CHECK(ctx)
  return false
end
function INSTALL(ctx)
  -- Programmatic package
end
]]
        local recipe_path = ctx.tmp_dir .. "/recipe.lua"
        local f = io.open(recipe_path, "w")
        f:write(recipe_content)
        f:close()
        ctx.commit_fetch("recipe.lua")
      end
    }
  }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
