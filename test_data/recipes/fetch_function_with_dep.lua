-- Test recipe with fetch function that depends on another recipe
IDENTITY = "local.fetcher_with_dep@v1"
DEPENDENCIES = {
  { recipe = "local.tool@v1", source = "tool.lua" }
}

function FETCH(ctx)
  -- Fetch phase uses a dependency
end

function INSTALL(ctx)
  -- Install from fetched materials
end
