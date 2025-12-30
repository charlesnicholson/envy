-- Test imperative stage using envy.extract for single file
IDENTITY = "local.stage_extract_single@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  -- Extract single archive with specific options
  local files = envy.extract(fetch_dir .. "/test.tar.gz", stage_dir, {strip = 1})
  -- files should be 5 (the number of regular files extracted)
end
