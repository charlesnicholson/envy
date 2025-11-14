-- Tests diamond with mixed needed_by phases
-- A depends on B (needed_by="fetch") and C (needed_by="build")
identity = "local.needed_by_diamond_a@v1"

dependencies = {
  { recipe = "local.needed_by_diamond_b@v1", source = "needed_by_diamond_b.lua", needed_by = "fetch" },
  { recipe = "local.needed_by_diamond_c@v1", source = "needed_by_diamond_c.lua", needed_by = "build" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end

build = function(ctx)
  -- Can access both B and C in build phase
  ctx.asset("local.needed_by_diamond_b@v1")
  ctx.asset("local.needed_by_diamond_c@v1")
end
