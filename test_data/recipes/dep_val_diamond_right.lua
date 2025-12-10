-- Right path for diamond dependency test
IDENTITY = "local.dep_val_diamond_right@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_diamond_base@v1", source = "dep_val_diamond_base.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
  ctx.asset("local.dep_val_diamond_base@v1")
end
