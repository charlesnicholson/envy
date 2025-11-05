-- Test recipe with fetch function that depends on another recipe
dependencies = {
  { recipe = "local.tool@1.0.0", file = "tool.lua" }
}

function fetch(ctx)
  -- Fetch phase uses a dependency
end

function install(ctx)
  -- Install from fetched materials
end
