-- Tests invalid needed_by phase name - should fail during parsing
identity = "local.needed_by_invalid@v1"

dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua", needed_by = "nonexistent" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
