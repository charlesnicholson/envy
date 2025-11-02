-- Invalid: non-local recipe trying to depend on local.*
dependencies = {
  { recipe = "local.simple@1.0.0", file = "simple.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
