-- Recipe with a dependency that has fetch prerequisites
identity = "local.simple_fetch_dep_parent@v1"

dependencies = {
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
identity = "local.simple_fetch_dep_child@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
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

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
