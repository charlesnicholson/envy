-- Fan-out child 2
IDENTITY = "local.fanout_child2@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
