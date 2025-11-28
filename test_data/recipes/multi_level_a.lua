identity = "local.multi_level_a@v1"

dependencies = {
  {
    recipe = "local.multi_level_b@v1",
    source = {
      dependencies = {
        { recipe = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(ctx, opts)
        local recipe_content = [=[
identity = "local.multi_level_b@v1"
dependencies = {
  {
    recipe = "local.multi_level_c@v1",
    source = {
      dependencies = {
        { recipe = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(ctx, opts)
        local recipe_content_c = [[
identity = "local.multi_level_c@v1"
dependencies = {}
function check(ctx)
  return false
end
function install(ctx)
  -- Programmatic package
end
]]
        local recipe_path_c = ctx.tmp_dir .. "/recipe.lua"
        local f_c = io.open(recipe_path_c, "w")
        f_c:write(recipe_content_c)
        f_c:close()
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
]=]
        local recipe_path = ctx.tmp_dir .. "/recipe.lua"
        local f = io.open(recipe_path, "w")
        f:write(recipe_content)
        f:close()
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
