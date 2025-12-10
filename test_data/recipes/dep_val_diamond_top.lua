-- Top of diamond dependency test
-- Tests: A→B→D, A→C→D, A accesses D via both paths
IDENTITY = "local.dep_val_diamond_top@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_diamond_left@v1", source = "dep_val_diamond_left.lua", needed_by = "stage" },
  { recipe = "local.dep_val_diamond_right@v1", source = "dep_val_diamond_right.lua", needed_by = "stage" },
  -- Must explicitly declare all dependencies we access
  { recipe = "local.dep_val_diamond_base@v1", source = "dep_val_diamond_base.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Access diamond base through both left and right paths
  ctx.asset("local.dep_val_diamond_base@v1")
end
