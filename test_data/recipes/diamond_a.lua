-- Top of diamond: A depends on B and C (which both depend on D)
dependencies = {
  { recipe = "local.diamond_b@1.0.0", file = "diamond_b.lua" },
  { recipe = "local.diamond_c@1.0.0", file = "diamond_c.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
