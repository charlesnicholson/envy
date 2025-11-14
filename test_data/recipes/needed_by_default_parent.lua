-- Tests default needed_by behavior - should default to "check" phase
identity = "local.needed_by_default_parent@v1"

dependencies = {
  -- No needed_by specified - should default to check
  { recipe = "local.simple@v1", source = "simple.lua" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
  -- Dependency is available
  ctx.asset("local.simple@v1")
end
