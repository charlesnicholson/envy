-- Recipe A with weak reference that creates cycle after resolution
IDENTITY = "local.weak_cycle_a@v1"
DEPENDENCIES = {
  { recipe = "foo" },  -- Weak ref-only, will match weak_cycle_b
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install
end
