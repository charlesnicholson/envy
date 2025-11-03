-- Recipe with same dependency but different options
dependencies = {
  { recipe = "local.with_options@1.0.0", file = "with_options.lua", options = { variant = "foo" } },
  { recipe = "local.with_options@1.0.0", file = "with_options.lua", options = { variant = "bar" } }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
