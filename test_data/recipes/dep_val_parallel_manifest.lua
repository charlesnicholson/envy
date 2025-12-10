-- Manifest recipe that depends on all 10 parallel users
IDENTITY = "local.dep_val_parallel_manifest@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_parallel_user1@v1", source = "dep_val_parallel_user1.lua" },
  { recipe = "local.dep_val_parallel_user2@v1", source = "dep_val_parallel_user2.lua" },
  { recipe = "local.dep_val_parallel_user3@v1", source = "dep_val_parallel_user3.lua" },
  { recipe = "local.dep_val_parallel_user4@v1", source = "dep_val_parallel_user4.lua" },
  { recipe = "local.dep_val_parallel_user5@v1", source = "dep_val_parallel_user5.lua" },
  { recipe = "local.dep_val_parallel_user6@v1", source = "dep_val_parallel_user6.lua" },
  { recipe = "local.dep_val_parallel_user7@v1", source = "dep_val_parallel_user7.lua" },
  { recipe = "local.dep_val_parallel_user8@v1", source = "dep_val_parallel_user8.lua" },
  { recipe = "local.dep_val_parallel_user9@v1", source = "dep_val_parallel_user9.lua" },
  { recipe = "local.dep_val_parallel_user10@v1", source = "dep_val_parallel_user10.lua" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
