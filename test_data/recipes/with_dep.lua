-- Recipe with a dependency
identity = "local.withdep@v1"
dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package - no cache interaction
end
