-- Fallback that should be ignored when a strong match exists
identity = "local.unused_fallback@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

