-- Strong dependency that should satisfy weak queries
identity = "local.existing_dep@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

