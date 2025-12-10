-- Test recipe with basic fetch function
IDENTITY = "local.fetcher@v1"
DEPENDENCIES = {}

function FETCH(ctx)
  -- Simulates fetching by writing a test file
  -- In real usage, would download archives, clone repos, etc.
end

function INSTALL(ctx)
  -- Install from fetched materials
end
