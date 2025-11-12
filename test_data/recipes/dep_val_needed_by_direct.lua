-- Recipe with needed_by="fetch" accessing direct dependency in fetch phase
identity = "local.dep_val_needed_by_direct@v1"

dependencies = {
  { recipe = "local.dep_val_needed_by_base@v1", source = "dep_val_needed_by_base.lua", needed_by = "fetch" }
}

fetch = function(ctx)
  -- Access direct dependency in fetch phase
  ctx.asset("local.dep_val_needed_by_base@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx)
  ctx.extract_all({strip = 1})
end
