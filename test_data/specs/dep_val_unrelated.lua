-- Test for unrelated spec error
-- This spec tries to access lib without declaring it
IDENTITY = "local.dep_val_unrelated@v1"

-- Intentionally NOT declaring any dependencies

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Try to access lib without declaring it - should fail
  envy.package("local.dep_val_lib@v1")
end
