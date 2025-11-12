-- Recipe A in a cycle: A -> B
identity = "local.cycle_a@v1"
dependencies = {
  { recipe = "local.cycle_b@v1", source = "cycle_b.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
