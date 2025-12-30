IDENTITY = "local.multi_level_a@v1"

DEPENDENCIES = {
  {
    spec = "local.multi_level_b@v1",
    source = {
      dependencies = {
        { spec = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(tmp_dir, options)
        local recipe_content = [=[
IDENTITY = "local.multi_level_b@v1"
DEPENDENCIES = {
  {
    spec = "local.multi_level_c@v1",
    source = {
      dependencies = {
        { spec = "local.simple_fetch_dep_base@v1", source = "simple_fetch_dep_base.lua" }
      },
      fetch = function(tmp_dir, options)
        local recipe_content_c = [[
IDENTITY = "local.multi_level_c@v1"
DEPENDENCIES = {}
function CHECK(project_root, options)
  return false
end
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
]]
        local recipe_path_c = tmp_dir .. "/spec.lua"
        local f_c = io.open(recipe_path_c, "w")
        f_c:write(recipe_content_c)
        f_c:close()
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
]=]
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
