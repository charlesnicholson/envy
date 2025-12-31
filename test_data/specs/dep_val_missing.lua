-- Dependency validation test: NEGATIVE - calls envy.package without declaring dependency
IDENTITY = "local.dep_val_missing@v1"

-- Note: NO dependencies declared, but we try to access dep_val_lib below

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  -- Try to access lib without declaring it - SHOULD FAIL
  local lib_path = envy.package("local.dep_val_lib@v1")
  envy.run([[echo "should not get here" > bad.txt]])
end
