-- Minimal test with dependency
identity = "local.test_minimal_dep@v1"

dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua" }
}

fetch = function(ctx)
  ctx.run("echo 'testing ctx.run in fetch'")
end

install = function(ctx)
  -- Programmatic package
end
