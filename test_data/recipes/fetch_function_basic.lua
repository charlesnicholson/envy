-- Test recipe with basic fetch function
IDENTITY = "local.fetcher@v1"
DEPENDENCIES = {}

function FETCH(tmp_dir, options)
  -- Simulates fetching by writing a test file
  -- In real usage, would download archives, clone repos, etc.
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Install from fetched materials
end
