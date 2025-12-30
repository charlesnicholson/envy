-- Top layer for 3-level transitive dependency test
-- Tests: A→B→C, A accesses C (3 levels)
IDENTITY = "local.dep_val_level3_top@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_level3_mid@v1", source = "dep_val_level3_mid.lua", needed_by = "stage" },
  -- Must explicitly declare all dependencies we access
  { spec = "local.dep_val_level3_base@v1", source = "dep_val_level3_base.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Access transitive dependency 2 levels deep
  envy.asset("local.dep_val_level3_base@v1")
end
