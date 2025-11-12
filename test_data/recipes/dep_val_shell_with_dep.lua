-- Recipe with default_shell function that calls ctx.asset() on declared dependency
identity = "local.dep_val_shell_with_dep@v1"

dependencies = {
  { recipe = "local.dep_val_shell_tool@v1", source = "dep_val_shell_tool.lua" }
}

default_shell = function(ctx)
  -- Access declared dependency in default_shell
  ctx.asset("local.dep_val_shell_tool@v1")
  return ENVY_SHELL.BASH
end

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
  -- Run a command to trigger default_shell evaluation
  ctx.run("echo 'test'")
end
