-- Tests default needed_by behavior - should default to "check" phase
IDENTITY = "local.needed_by_default_parent@v1"

DEPENDENCIES = {
  -- No needed_by specified - should default to check
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

BUILD = function(ctx, opts)
  -- Dependency should be available by build phase
  ctx.asset("local.dep_val_lib@v1")
end
