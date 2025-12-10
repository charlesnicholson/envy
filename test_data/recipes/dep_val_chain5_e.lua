-- Level E (top) for 5-level chain test
-- Tests: E→D→C→B→A, E accesses A (5 levels deep)
IDENTITY = "local.dep_val_chain5_e@v1"

DEPENDENCIES = {
  { recipe = "local.dep_val_chain5_d@v1", source = "dep_val_chain5_d.lua", needed_by = "stage" },
  -- Must explicitly declare all dependencies we access
  { recipe = "local.dep_val_chain5_a@v1", source = "dep_val_chain5_a.lua", needed_by = "stage" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Access dependency 4 levels deep
  ctx.asset("local.dep_val_chain5_a@v1")
end
