-- Minimal test recipe - no dependencies
identity = "local.simple@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package - no cache interaction
end
