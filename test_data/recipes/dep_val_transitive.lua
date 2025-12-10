-- Dependency validation test: POSITIVE - transitive dependency access
IDENTITY = "local.dep_val_transitive@v1"

DEPENDENCIES = {
  -- We depend on tool, which depends on lib
  { recipe = "local.dep_val_tool@v1", source = "dep_val_tool.lua" },
  -- In the new design, we must explicitly declare all dependencies we use
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
  -- Access tool (direct dependency) - should work
  local tool_path = ctx.asset("local.dep_val_tool@v1")

  -- Access lib (transitive dependency: us → tool → lib) - SHOULD WORK
  local lib_path = ctx.asset("local.dep_val_lib@v1")

  ctx.run([[echo "transitive access worked" > transitive.txt]])
end
