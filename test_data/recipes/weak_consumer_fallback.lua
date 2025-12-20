-- Weak dependency with fallback when the target is absent
IDENTITY = "local.weak_consumer_fallback@v1"
DEPENDENCIES = {
  { recipe = "local.missing_dep", weak = { recipe = "local.weak_fallback@v1", source = "weak_fallback.lua" } },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end

