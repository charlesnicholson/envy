-- Recipe with wrong identity declaration (mismatch)
IDENTITY = "local.wrong_identity@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- This should never execute
end
