-- Reference-only dependency with no provider anywhere in the graph
IDENTITY = "local.weak_missing_ref@v1"
DEPENDENCIES = {
  { spec = "local.never_provided" },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end

