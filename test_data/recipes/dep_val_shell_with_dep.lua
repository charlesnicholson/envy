-- Recipe with default_shell function that calls ctx.asset() on declared dependency
IDENTITY = "local.dep_val_shell_with_dep@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_shell_tool@v1", source = "dep_val_shell_tool.lua" }
}

DEFAULT_SHELL = function(ctx, opts)
  -- Access declared dependency in default_shell
  ctx.asset("local.dep_val_shell_tool@v1")
  return ENVY_SHELL.BASH
end

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Run a command to trigger default_shell evaluation
  ctx.run("echo 'test'")
end
