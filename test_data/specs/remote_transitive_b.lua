-- remote.b@v1
-- Remote spec that depends on local.* (transitively violates security)

IDENTITY = "remote.b@v1"
DEPENDENCIES = {
  {
    spec = "local.c@v1",
    source = "local_c.lua"
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing remote transitive b recipe")
end
