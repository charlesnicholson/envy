-- Dependency validation test: POSITIVE - transitive dependency access
identity = "local.dep_val_transitive@v1"

dependencies = {
  -- We depend on tool, which depends on lib
  -- So we should be able to access lib transitively
  { recipe = "local.dep_val_tool@v1", file = "dep_val_tool.lua" }
}

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end

build = function(ctx)
  -- Access tool (direct dependency) - should work
  local tool_path = ctx.asset("local.dep_val_tool@v1")

  -- Access lib (transitive dependency: us → tool → lib) - SHOULD WORK
  local lib_path = ctx.asset("local.dep_val_lib@v1")

  ctx.run([[echo "transitive access worked" > transitive.txt]])
end
