-- Weak reference whose fallback introduces another weak reference (multi-iteration)
IDENTITY = "local.weak_chain_root@v1"
DEPENDENCIES = {
  { spec = "local.chain_missing", weak = { spec = "local.chain_b@v1", source = "weak_chain_b.lua" } },
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic install: no cache artifacts
end

