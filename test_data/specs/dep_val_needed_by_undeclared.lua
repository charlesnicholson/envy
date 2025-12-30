-- Recipe that uses needed_by="fetch" but tries to access undeclared dependency
IDENTITY = "local.dep_val_needed_by_undeclared@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_needed_by_mid@v1", source = "dep_val_needed_by_mid.lua", needed_by = "fetch" }
}

FETCH = function(tmp_dir, options)
  -- Try to access lib which is NOT declared as dependency - should fail
  envy.asset("local.dep_val_lib@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
