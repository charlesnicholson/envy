-- Top of diamond: A depends on B and C (which both depend on D)
identity = "local.diamond_a@v1"
dependencies = {
  { recipe = "local.diamond_b@v1", file = "diamond_b.lua" },
  { recipe = "local.diamond_c@v1", file = "diamond_c.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
