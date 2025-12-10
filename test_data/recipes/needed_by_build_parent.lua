-- Tests needed_by="build" - dependency completes before parent's build phase
IDENTITY = "local.needed_by_build_parent@v1"

DEPENDENCIES = {
  { recipe = "local.needed_by_build_dep@v1", source = "needed_by_build_dep.lua", needed_by = "build" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

BUILD = function(ctx, opts)
  -- Can access dependency in build phase
  ctx.asset("local.needed_by_build_dep@v1")
  ctx.run("echo 'build complete' > build.txt")
end
