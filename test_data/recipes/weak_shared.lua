-- Shared dependency produced by fallbacks
identity = "local.shared@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

