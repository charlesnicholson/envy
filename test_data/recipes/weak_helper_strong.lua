-- Strong provider for helper identity
identity = "local.helper@v1"

function check(ctx)
  return true
end

function install(ctx)
  -- No-op; check returns true so install is skipped.
end
