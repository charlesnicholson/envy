-- Recipe that depends on itself (self-loop)
IDENTITY = "local.self_dep@v1"
DEPENDENCIES = {
  { recipe = "local.self_dep@v1", source = "self_dep.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
