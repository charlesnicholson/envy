-- Middle spec for needed_by transitive testing
IDENTITY = "local.dep_val_needed_by_mid@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_needed_by_base@v1", source = "dep_val_needed_by_base.lua" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
