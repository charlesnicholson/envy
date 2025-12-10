-- Wide fan-out: root depends on many children
IDENTITY = "local.fanout_root@v1"
DEPENDENCIES = {
  { recipe = "local.fanout_child1@v1", source = "fanout_child1.lua" },
  { recipe = "local.fanout_child2@v1", source = "fanout_child2.lua" },
  { recipe = "local.fanout_child3@v1", source = "fanout_child3.lua" },
  { recipe = "local.fanout_child4@v1", source = "fanout_child4.lua" }
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic package
end
