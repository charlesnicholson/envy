-- Tests spec with multiple dependencies using different needed_by phases
IDENTITY = "local.needed_by_all_phases@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_fetch_dep@v1", source = "needed_by_fetch_dep.lua", needed_by = "fetch" },
  { spec = "local.needed_by_check_dep@v1", source = "needed_by_check_dep.lua", needed_by = "check" },
  { spec = "local.needed_by_stage_dep@v1", source = "needed_by_stage_dep.lua", needed_by = "stage" },
  { spec = "local.needed_by_build_dep@v1", source = "needed_by_build_dep.lua", needed_by = "build" },
  { spec = "local.needed_by_install_dep@v1", source = "needed_by_install_dep.lua", needed_by = "install" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  -- Access all dependencies
  envy.asset("local.needed_by_fetch_dep@v1")
  envy.asset("local.needed_by_check_dep@v1")
  envy.asset("local.needed_by_stage_dep@v1")
  envy.asset("local.needed_by_build_dep@v1")
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  envy.asset("local.needed_by_install_dep@v1")
end
