-- Fan-out child 1
identity = "local.fanout_child1@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
