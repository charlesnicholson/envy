-- Root recipe that loads both A and B before weak resolution
identity = "local.weak_cycle_root@v1"
dependencies = {
  { recipe = "local.weak_cycle_a@v1", source = "weak_cycle_a.lua" },
  { recipe = "local.foo@v1", source = "weak_cycle_b.lua" },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install
end
