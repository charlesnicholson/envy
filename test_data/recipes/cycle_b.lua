-- Recipe B in a cycle: B -> A
identity = "local.cycle_b@v1"
dependencies = {
  { recipe = "local.cycle_a@v1", source = "cycle_a.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
