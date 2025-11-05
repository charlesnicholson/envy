-- Independent tree, no shared deps
identity = "local.independent_right@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
