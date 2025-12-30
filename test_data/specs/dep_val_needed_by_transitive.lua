-- Recipe that uses needed_by="fetch" and accesses transitive dependency in fetch phase
IDENTITY = "local.dep_val_needed_by_transitive@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_needed_by_mid@v1", source = "dep_val_needed_by_mid.lua", needed_by = "fetch" },
  -- Must explicitly declare all dependencies we access
  { spec = "local.dep_val_needed_by_base@v1", source = "dep_val_needed_by_base.lua", needed_by = "fetch" }
}

FETCH = function(tmp_dir, options)
  -- Access transitive dependency (mid→base) in fetch phase
  envy.asset("local.dep_val_needed_by_base@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
