-- Base spec that will be a fetch dependency for another recipe
IDENTITY = "local.simple_fetch_dep_base@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
