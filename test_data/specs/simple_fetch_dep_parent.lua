-- Recipe with a dependency that has fetch prerequisites
IDENTITY = "local.simple_fetch_dep_parent@v1"

DEPENDENCIES = {
  {
    spec = "local.simple_fetch_dep_child@v1",
    source = {
      dependencies = {
        { spec = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(tmp_dir, options)
        -- Base recipe is guaranteed to be installed before this runs
        -- Write the spec.lua for the child recipe
        local recipe_content = [[
IDENTITY = "local.simple_fetch_dep_child@v1"
DEPENDENCIES = {}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
]]
        local recipe_path = tmp_dir .. "/spec.lua"
        local f = io.open(recipe_path, "w")
        f:write(recipe_content)
        f:close()

        -- Commit the recipe file to the fetch_dir
        envy.commit_fetch("spec.lua")
      end
    }
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
