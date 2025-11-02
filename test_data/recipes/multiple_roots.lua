-- Recipe with two independent dependencies
dependencies = {
  { recipe = "local.independent_left@1.0.0", file = "independent_left.lua" },
  { recipe = "local.independent_right@1.0.0", file = "independent_right.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
