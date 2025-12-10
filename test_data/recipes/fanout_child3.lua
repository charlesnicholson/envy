-- Fan-out child 3
IDENTITY = "local.fanout_child3@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
