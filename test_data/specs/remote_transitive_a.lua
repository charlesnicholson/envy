-- remote.a@v1
-- Remote recipe that transitively depends on local.* through remote.b

IDENTITY = "remote.a@v1"
DEPENDENCIES = {
  {
    spec = "remote.b@v1",
    source = "remote_transitive_b.lua"
    -- No SHA256 (permissive mode - for testing)
  }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.info("Installing remote transitive a recipe")
end
