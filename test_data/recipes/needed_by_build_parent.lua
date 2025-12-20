-- Tests needed_by="build" - dependency completes before parent's build phase
IDENTITY = "local.needed_by_build_parent@v1"

DEPENDENCIES = {
  { recipe = "local.needed_by_build_dep@v1", source = "needed_by_build_dep.lua", needed_by = "build" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end

BUILD = function(stage_dir, fetch_dir, tmp_dir, options)
  -- Can access dependency in build phase
  envy.asset("local.needed_by_build_dep@v1")
  envy.run("echo 'build complete' > build.txt")
end
