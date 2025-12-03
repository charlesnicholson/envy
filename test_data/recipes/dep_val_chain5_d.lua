-- Level D for 5-level chain test
identity = "local.dep_val_chain5_d@v1"

dependencies = {
  { recipe = "local.dep_val_chain5_c@v1", source = "dep_val_chain5_c.lua", needed_by = "stage" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
  ctx.asset("local.dep_val_chain5_c@v1")
end
