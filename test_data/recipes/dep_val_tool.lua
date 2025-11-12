-- Dependency validation test: tool that depends on lib
identity = "local.dep_val_tool@v1"

dependencies = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end

build = function(ctx)
  -- Access our direct dependency - should work
  local lib_path = ctx.asset("local.dep_val_lib@v1")
  ctx.run([[echo "tool built with lib" > tool.txt]])
end
