-- Weak dependency with a present strong match and unused fallback
IDENTITY = "local.weak_consumer_existing@v1"
DEPENDENCIES = {
  { spec = "local.existing_dep@v1", source = "weak_existing_dep.lua" },
  { spec = "local.existing_dep", weak = { spec = "local.unused_fallback@v1", source = "weak_unused_fallback.lua" } },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end

