-- Tests needed_by="check" - dependency completes before parent's check phase
IDENTITY = "local.needed_by_check_parent@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_check_dep@v1", source = "needed_by_check_dep.lua", needed_by = "check" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Dependency is available by now since check already completed
  envy.asset("local.needed_by_check_dep@v1")
end
