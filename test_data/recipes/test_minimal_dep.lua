-- Minimal test with dependency
IDENTITY = "local.test_minimal_dep@v1"

DEPENDENCIES = {
  { recipe = "local.simple@v1", source = "simple.lua" }
}

FETCH = function(ctx, opts)
  ctx.run("echo 'testing ctx.run in fetch'")
end

INSTALL = function(ctx, opts)
  -- Programmatic package
end
