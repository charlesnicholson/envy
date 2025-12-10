-- Fan-out child 4
IDENTITY = "local.fanout_child4@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
