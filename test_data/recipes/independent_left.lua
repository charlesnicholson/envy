-- Independent tree, no shared deps
IDENTITY = "local.independent_left@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
