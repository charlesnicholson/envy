-- Recipe with default_shell function that calls ctx.asset() on missing dependency
identity = "local.dep_val_shell_missing_dep@v1"

-- Depend on tool, making lib available in the graph (tool depends on lib)
-- But we'll try to access an unrelated recipe that's also in the graph
dependencies = {
  { recipe = "local.dep_val_tool@v1", file = "dep_val_tool.lua" },
  -- Also add unrelated to graph so it exists but is not our dependency
  { recipe = "local.dep_val_unrelated@v1", file = "dep_val_unrelated.lua" }
}

default_shell = function(ctx)
  -- Try to access transitive dep (tool→lib) - this would actually be allowed
  -- So instead access another recipe in the tree
  local tool_path = ctx.asset("local.dep_val_tool@v1")
  -- Now try to access unrelated's dependency (lib) which we haven't declared
  ctx.asset("local.dep_val_lib@v1")
  return ENVY_SHELL.BASH
end

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
  -- Run a command to trigger default_shell evaluation
  ctx.run("echo 'test'")
end
