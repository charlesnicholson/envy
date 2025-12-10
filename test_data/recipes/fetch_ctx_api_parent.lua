IDENTITY = "local.fetch_ctx_api_parent@v1"

DEPENDENCIES = {
  {
    recipe = "local.fetch_ctx_api_child@v1",
    source = {
      dependencies = {
        { recipe = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(ctx, opts)
        -- Create test content and write to a file
        local test_content = "test data for verification"
        local test_path = ctx.tmp_dir .. "/test_source.txt"
        local f = io.open(test_path, "w")
        f:write(test_content)
        f:close()

        -- Use ctx.fetch to copy the file
        ctx.fetch(test_path)

        -- Create recipe.lua
        local recipe_content = [[
IDENTITY = "local.fetch_ctx_api_child@v1"
DEPENDENCIES = {}
function CHECK(ctx)
  return false
end
function INSTALL(ctx)
  -- Programmatic package
end
]]
        local recipe_path = ctx.tmp_dir .. "/recipe.lua"
        f = io.open(recipe_path, "w")
        f:write(recipe_content)
        f:close()

        -- Commit the recipe file
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
