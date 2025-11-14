-- Tests needed_by with cache hits - dependency should be cached on second run
identity = "local.needed_by_cached_parent@v1"

dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua", needed_by = "build" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end

build = function(ctx)
  -- Access simple in build phase
  ctx.asset("local.simple@v1")
end
