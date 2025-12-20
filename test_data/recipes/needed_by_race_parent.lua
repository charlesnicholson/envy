-- Tests race condition where dependency completes before parent discovers it
-- Uses a very simple/fast dependency to increase chance of race
IDENTITY = "local.needed_by_race_parent@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Access simple which may have already completed
  envy.asset("local.dep_val_lib@v1")
end
