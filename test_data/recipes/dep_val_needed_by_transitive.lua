-- Recipe that uses needed_by="fetch" and accesses transitive dependency in fetch phase
identity = "local.dep_val_needed_by_transitive@v1"

dependencies = {
  { recipe = "local.dep_val_needed_by_mid@v1", source = "dep_val_needed_by_mid.lua", needed_by = "fetch" },
  -- Must explicitly declare all dependencies we access
  { recipe = "local.dep_val_needed_by_base@v1", source = "dep_val_needed_by_base.lua", needed_by = "fetch" }
}

fetch = function(ctx)
  -- Access transitive dependency (mid→base) in fetch phase
  ctx.asset("local.dep_val_needed_by_base@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx)
  ctx.extract_all({strip = 1})
end
