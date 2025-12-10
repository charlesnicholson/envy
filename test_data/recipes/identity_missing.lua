-- Recipe missing identity declaration (invalid)
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- This should never execute
end
