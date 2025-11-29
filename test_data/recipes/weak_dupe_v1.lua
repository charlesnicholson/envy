-- First candidate for ambiguity tests
identity = "local.dupe@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

