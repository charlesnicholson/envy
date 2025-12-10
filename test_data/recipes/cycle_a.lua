-- Recipe A in a cycle: A -> B
IDENTITY = "local.cycle_a@v1"
DEPENDENCIES = {
  { recipe = "local.cycle_b@v1", source = "cycle_b.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
