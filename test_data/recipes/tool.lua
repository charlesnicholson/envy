-- Minimal tool recipe used as dependency
identity = "local.tool@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Install tool
end
