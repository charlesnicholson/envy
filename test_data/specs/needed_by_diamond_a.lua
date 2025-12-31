-- Tests diamond with mixed needed_by phases
-- A depends on B (needed_by="fetch") and C (needed_by="build")
IDENTITY = "local.needed_by_diamond_a@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_diamond_b@v1", source = "needed_by_diamond_b.lua", needed_by = "fetch" },
  { spec = "local.needed_by_diamond_c@v1", source = "needed_by_diamond_c.lua", needed_by = "build" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  -- Can access both B and C in build phase
  envy.package("local.needed_by_diamond_b@v1")
  envy.package("local.needed_by_diamond_c@v1")
end
