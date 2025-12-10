-- Recipe with same dependency but different options
IDENTITY = "local.options_parent@v1"
DEPENDENCIES = {
  { recipe = "local.with_options@v1", source = "with_options.lua", options = { variant = "foo" } },
  { recipe = "local.with_options@v1", source = "with_options.lua", options = { variant = "bar" } }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
