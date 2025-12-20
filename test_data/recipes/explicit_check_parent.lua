-- Tests explicit needed_by="check" - dependency completes before parent's check phase
IDENTITY = "local.explicit_check_parent@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua", needed_by = "check" }
}

FETCH = function(tmp_dir, options)
  -- Dependency will complete by check phase (before fetch)
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
