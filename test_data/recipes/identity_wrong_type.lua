-- Recipe with identity as wrong type (table instead of string)
IDENTITY = { name = "wrong" }
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- This should never execute
end
