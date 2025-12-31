-- Level D for 5-level chain test
IDENTITY = "local.dep_val_chain5_d@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_chain5_c@v1", source = "dep_val_chain5_c.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  envy.package("local.dep_val_chain5_c@v1")
end
