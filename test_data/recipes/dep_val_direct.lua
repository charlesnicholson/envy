-- Dependency validation test: POSITIVE - direct dependency access
identity = "local.dep_val_direct@v1"

dependencies = {
  { recipe = "local.dep_val_lib@v1", file = "dep_val_lib.lua" }
}

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end

build = function(ctx)
  -- Access direct dependency - SHOULD WORK
  local lib_path = ctx.asset("local.dep_val_lib@v1")
  ctx.run([[echo "direct access worked" > direct.txt]])
end
