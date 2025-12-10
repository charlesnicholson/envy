-- Middle layer for 3-level transitive dependency test
IDENTITY = "local.dep_val_level3_mid@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_level3_base@v1", source = "dep_val_level3_base.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Access declared dependency
  ctx.asset("local.dep_val_level3_base@v1")
end
