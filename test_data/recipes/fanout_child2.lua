-- Fan-out child 2
identity = "local.fanout_child2@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
