-- Minimal test with dependency
IDENTITY = "local.test_minimal_dep@v1"

DEPENDENCIES = {
  { spec = "local.simple@v1", source = "simple.lua" }
}

FETCH = function(tmp_dir, options)
  envy.run("echo 'testing ctx.run in fetch'")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
