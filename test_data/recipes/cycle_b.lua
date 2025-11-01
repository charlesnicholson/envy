-- Recipe B in a cycle: B -> A
dependencies = {
  { recipe = "local.cycle_a@v1", file = "cycle_a.lua" }
}
