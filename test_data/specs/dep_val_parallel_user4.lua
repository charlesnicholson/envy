-- User recipe 4 for parallel validation testing
IDENTITY = "local.dep_val_parallel_user4@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_parallel_base@v1", source = "dep_val_parallel_base.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Access shared base library
  envy.asset("local.dep_val_parallel_base@v1")
end
