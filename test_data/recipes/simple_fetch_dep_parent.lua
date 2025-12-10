-- Recipe with a dependency that has fetch prerequisites
IDENTITY = "local.simple_fetch_dep_parent@v1"

DEPENDENCIES = {
  {
    recipe = "local.simple_fetch_dep_child@v1",
    source = {
      dependencies = {
        { recipe = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(ctx)
        -- Base recipe is guaranteed to be installed before this runs
        -- Write the recipe.lua for the child recipe
        local recipe_content = [[
IDENTITY = "local.simple_fetch_dep_child@v1"
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

        -- Commit the recipe file to the fetch_dir
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
