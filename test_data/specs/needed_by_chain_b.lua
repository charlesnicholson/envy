-- Tests multi-level chain - middle node (depends on C with needed_by="build")
IDENTITY = "local.needed_by_chain_b@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_chain_c@v1", source = "needed_by_chain_c.lua", needed_by = "build" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  -- Can access chain_c in build phase
  envy.package("local.needed_by_chain_c@v1")
end
