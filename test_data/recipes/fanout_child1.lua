-- Fan-out child 1
IDENTITY = "local.fanout_child1@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
