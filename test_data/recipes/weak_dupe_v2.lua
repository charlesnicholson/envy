-- Second candidate for ambiguity tests
identity = "local.dupe@v2"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

