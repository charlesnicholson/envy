-- Test recipe with fetch function that depends on another recipe
identity = "local.fetcher_with_dep@v1"
dependencies = {
  { recipe = "local.tool@v1", file = "tool.lua" }
}

function fetch(ctx)
  -- Fetch phase uses a dependency
end

function install(ctx)
  -- Install from fetched materials
end
