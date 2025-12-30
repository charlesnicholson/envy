-- Reference-only dependency that matches multiple strong candidates
IDENTITY = "local.weak_consumer_ambiguous@v1"
DEPENDENCIES = {
  { spec = "local.dupe@v1", source = "weak_dupe_v1.lua" },
  { spec = "local.dupe@v2", source = "weak_dupe_v2.lua" },
  { spec = "local.dupe" },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end

