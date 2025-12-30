-- Tests invalid needed_by phase name - should fail during parsing
IDENTITY = "local.needed_by_invalid@v1"

DEPENDENCIES = {
  { spec = "local.simple@v1", source = "simple.lua", needed_by = "nonexistent" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
