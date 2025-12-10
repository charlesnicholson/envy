-- Fallback helper for weak custom fetch dependency
IDENTITY = "local.helper.fallback@v1"

function CHECK(ctx)
  return true
end

function INSTALL(ctx)
  -- No-op for helper fallback; check returns true so install is skipped.
end
