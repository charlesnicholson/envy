-- Recipe with same dependency but different options
IDENTITY = "local.options_parent@v1"
DEPENDENCIES = {
  { spec = "local.with_options@v1", source = "with_options.lua", options = { variant = "foo" } },
  { spec = "local.with_options@v1", source = "with_options.lua", options = { variant = "bar" } }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
