-- Fallback recipe used when no provider is found
identity = "local.weak_fallback@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

