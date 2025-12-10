-- Strong provider for helper identity
IDENTITY = "local.helper@v1"

function CHECK(ctx)
  return true
end

function INSTALL(ctx)
  -- No-op; check returns true so install is skipped.
end
