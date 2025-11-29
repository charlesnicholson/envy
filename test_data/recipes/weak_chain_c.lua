-- Terminal dependency for the weak chain
identity = "local.chain_c@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

