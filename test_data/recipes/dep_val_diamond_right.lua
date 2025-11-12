-- Right path for diamond dependency test
identity = "local.dep_val_diamond_right@v1"

dependencies = {
  { recipe = "local.dep_val_diamond_base@v1", file = "dep_val_diamond_base.lua" }
}

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
  ctx.asset("local.dep_val_diamond_base@v1")
end
