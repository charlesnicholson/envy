-- Tests needed_by with cache hits - dependency should be cached on second run
IDENTITY = "local.needed_by_cached_parent@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua", needed_by = "build" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

BUILD = function(ctx, opts)
  -- Access simple in build phase
  ctx.asset("local.dep_val_lib@v1")
end
