-- Recipe that uses needed_by="fetch" but tries to access undeclared dependency
identity = "local.dep_val_needed_by_undeclared@v1"

dependencies = {
  { recipe = "local.dep_val_needed_by_mid@v1", source = "dep_val_needed_by_mid.lua", needed_by = "fetch" }
}

fetch = function(ctx, opts)
  -- Try to access lib which is NOT declared as dependency - should fail
  ctx.asset("local.dep_val_lib@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
