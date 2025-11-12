-- User recipe 9 for parallel validation testing
identity = "local.dep_val_parallel_user9@v1"

dependencies = {
  { recipe = "local.dep_val_parallel_base@v1", source = "dep_val_parallel_base.lua" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
  -- Access shared base library
  ctx.asset("local.dep_val_parallel_base@v1")
end
