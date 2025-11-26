-- Minimal test with dependency
identity = "local.test_minimal_dep@v1"

dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua" }
}

fetch = function(ctx, opts)
  ctx.run("echo 'testing ctx.run in fetch'")
end

install = function(ctx, opts)
  -- Programmatic package
end
