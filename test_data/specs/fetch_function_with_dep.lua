-- Test spec with fetch function that depends on another recipe
IDENTITY = "local.fetcher_with_dep@v1"
DEPENDENCIES = {
  { spec = "local.tool@v1", source = "tool.lua" }
}

function FETCH(tmp_dir, options)
  -- Fetch phase uses a dependency
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Install from fetched materials
end
