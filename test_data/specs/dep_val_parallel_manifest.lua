-- Manifest spec that depends on all 10 parallel users
IDENTITY = "local.dep_val_parallel_manifest@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_parallel_user1@v1", source = "dep_val_parallel_user1.lua" },
  { spec = "local.dep_val_parallel_user2@v1", source = "dep_val_parallel_user2.lua" },
  { spec = "local.dep_val_parallel_user3@v1", source = "dep_val_parallel_user3.lua" },
  { spec = "local.dep_val_parallel_user4@v1", source = "dep_val_parallel_user4.lua" },
  { spec = "local.dep_val_parallel_user5@v1", source = "dep_val_parallel_user5.lua" },
  { spec = "local.dep_val_parallel_user6@v1", source = "dep_val_parallel_user6.lua" },
  { spec = "local.dep_val_parallel_user7@v1", source = "dep_val_parallel_user7.lua" },
  { spec = "local.dep_val_parallel_user8@v1", source = "dep_val_parallel_user8.lua" },
  { spec = "local.dep_val_parallel_user9@v1", source = "dep_val_parallel_user9.lua" },
  { spec = "local.dep_val_parallel_user10@v1", source = "dep_val_parallel_user10.lua" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
