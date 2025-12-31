-- Spec with default_shell function that calls envy.package() on declared dependency
IDENTITY = "local.dep_val_shell_with_dep@v1"

DEPENDENCIES = {
  { spec = "local.dep_val_shell_tool@v1", source = "dep_val_shell_tool.lua" }
}

DEFAULT_SHELL = function(ctx, opts)
  -- Access declared dependency in default_shell
  envy.package("local.dep_val_shell_tool@v1")
  return ENVY_SHELL.BASH
end

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
  -- Run a command to trigger default_shell evaluation
  envy.run("echo 'test'")
end
