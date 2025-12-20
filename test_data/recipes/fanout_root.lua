-- Wide fan-out: root depends on many children
IDENTITY = "local.fanout_root@v1"
DEPENDENCIES = {
  { recipe = "local.fanout_child1@v1", source = "fanout_child1.lua" },
  { recipe = "local.fanout_child2@v1", source = "fanout_child2.lua" },
  { recipe = "local.fanout_child3@v1", source = "fanout_child3.lua" },
  { recipe = "local.fanout_child4@v1", source = "fanout_child4.lua" }
}

function CHECK(project_root, options)
  return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
