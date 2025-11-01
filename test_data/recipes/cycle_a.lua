-- Recipe A in a cycle: A -> B
dependencies = {
  { recipe = "local.cycle_b@v1", file = "cycle_b.lua" }
}
