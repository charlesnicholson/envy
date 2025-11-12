-- Recipe with two independent dependencies
identity = "local.multiple_roots@v1"
dependencies = {
  { recipe = "local.independent_left@v1", source = "independent_left.lua" },
  { recipe = "local.independent_right@v1", source = "independent_right.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
