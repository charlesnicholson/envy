-- Left path for diamond dependency test
IDENTITY = "local.dep_val_diamond_left@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_diamond_base@v1", source = "dep_val_diamond_base.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  envy.package("local.dep_val_diamond_base@v1")
end
