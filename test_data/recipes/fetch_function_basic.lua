-- Test recipe with basic fetch function
identity = "local.fetcher@v1"
dependencies = {}

function fetch(ctx)
  -- Simulates fetching by writing a test file
  -- In real usage, would download archives, clone repos, etc.
end

function install(ctx)
  -- Install from fetched materials
end
