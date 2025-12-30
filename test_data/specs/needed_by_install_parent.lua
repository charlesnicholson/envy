-- Tests needed_by="install" - dependency completes before parent's install phase
IDENTITY = "local.needed_by_install_parent@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_install_dep@v1", source = "needed_by_install_dep.lua", needed_by = "install" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Can access dependency in install phase
  envy.asset("local.needed_by_install_dep@v1")
end
