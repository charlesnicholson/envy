IDENTITY = "local.multiple_fetch_deps_parent@v1"

DEPENDENCIES = {
  {
    spec = "local.multiple_fetch_deps_child@v1",
    source = {
      dependencies = {
        { spec = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" },
        { spec = "local.fetch_dep_helper@v1", source = "fetch_dep_helper.lua" }
      },
      fetch = function(tmp_dir, options)
        local recipe_content = [[
IDENTITY = "local.multiple_fetch_deps_child@v1"
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
