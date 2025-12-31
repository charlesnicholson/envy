-- Tests multi-level chain - top node (depends on B with needed_by="stage")
IDENTITY = "local.needed_by_chain_a@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_chain_b@v1", source = "needed_by_chain_b.lua", needed_by = "stage" },
  -- Must declare transitive dependency C if we access it
  { spec = "local.needed_by_chain_c@v1", source = "needed_by_chain_c.lua" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Can access chain_b in stage phase
  envy.package("local.needed_by_chain_b@v1")
  -- Can access chain_c (transitively available)
  envy.package("local.needed_by_chain_c@v1")
end
