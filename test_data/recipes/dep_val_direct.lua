-- Dependency validation test: POSITIVE - direct dependency access
IDENTITY = "local.dep_val_direct@v1"

DEPENDENCIES = {
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
  -- Access direct dependency - SHOULD WORK
  local lib_path = ctx.asset("local.dep_val_lib@v1")
  ctx.run([[echo "direct access worked" > direct.txt]])
end
