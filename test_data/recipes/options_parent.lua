-- Recipe with same dependency but different options
identity = "local.options_parent@v1"
dependencies = {
  { recipe = "local.with_options@v1", file = "with_options.lua", options = { variant = "foo" } },
  { recipe = "local.with_options@v1", file = "with_options.lua", options = { variant = "bar" } }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
