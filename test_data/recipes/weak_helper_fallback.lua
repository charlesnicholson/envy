-- Fallback helper for weak custom fetch dependency
identity = "local.helper.fallback@v1"

function check(ctx)
  return true
end

function install(ctx)
  -- No-op for helper fallback; check returns true so install is skipped.
end
