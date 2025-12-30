-- Tests default needed_by - no explicit needed_by specified
IDENTITY = "local.default_needed_by_parent@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_lib@v1", source = "dep_val_lib.lua" }
  -- No needed_by specified - should default to "build"
}

FETCH = function(tmp_dir, options)
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  -- Dependency should be available here by default
  local dep_path = envy.asset("local.dep_val_lib@v1")
end
