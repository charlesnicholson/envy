-- Tests needed_by="build" - dependency completes before parent's build phase
identity = "local.needed_by_build_parent@v1"

dependencies = {
  { recipe = "local.needed_by_build_dep@v1", source = "needed_by_build_dep.lua", needed_by = "build" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

build = function(ctx, opts)
  -- Can access dependency in build phase
  ctx.asset("local.needed_by_build_dep@v1")
  ctx.run("echo 'build complete' > build.txt")
end
