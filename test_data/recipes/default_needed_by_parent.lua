-- Tests default needed_by - no explicit needed_by specified
IDENTITY = "local.default_needed_by_parent@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua" }
  -- No needed_by specified - should default to "build"
}

FETCH = function(ctx, opts)
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

BUILD = function(ctx, opts)
  -- Dependency should be available here by default
  local dep_path = ctx.asset("local.dep_val_lib@v1")
end
