-- Wide fan-out: root depends on many children
identity = "local.fanout_root@v1"
dependencies = {
  { recipe = "local.fanout_child1@v1", file = "fanout_child1.lua" },
  { recipe = "local.fanout_child2@v1", file = "fanout_child2.lua" },
  { recipe = "local.fanout_child3@v1", file = "fanout_child3.lua" },
  { recipe = "local.fanout_child4@v1", file = "fanout_child4.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
