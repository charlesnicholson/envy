-- Top of diamond dependency test
-- Tests: A→B→D, A→C→D, A accesses D via both paths
identity = "local.dep_val_diamond_top@v1"

dependencies = {
  { recipe = "local.dep_val_diamond_left@v1", file = "dep_val_diamond_left.lua" },
  { recipe = "local.dep_val_diamond_right@v1", file = "dep_val_diamond_right.lua" }
}

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
  -- Access diamond base through both left and right paths
  ctx.asset("local.dep_val_diamond_base@v1")
end
