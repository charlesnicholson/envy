-- Wide fan-out: root depends on many children
dependencies = {
  { recipe = "local.fanout_child1@1.0.0", file = "fanout_child1.lua" },
  { recipe = "local.fanout_child2@1.0.0", file = "fanout_child2.lua" },
  { recipe = "local.fanout_child3@1.0.0", file = "fanout_child3.lua" },
  { recipe = "local.fanout_child4@1.0.0", file = "fanout_child4.lua" }
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic package
end
