-- Recipe that depends on itself (self-loop)
identity = "local.self_dep@v1"
dependencies = {
  { recipe = "local.self_dep@v1", source = "self_dep.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
