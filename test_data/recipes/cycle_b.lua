-- Recipe B in a cycle: B -> A
dependencies = {
  { recipe = "local.cycle_a@v1", file = "cycle_a.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
