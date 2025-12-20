-- Reference-only dependency that is satisfied by an existing provider
IDENTITY = "local.weak_consumer_ref_only@v1"
DEPENDENCIES = {
  { recipe = "local.weak_provider@v1", source = "weak_provider.lua" },
  { recipe = "local.weak_provider" },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end

