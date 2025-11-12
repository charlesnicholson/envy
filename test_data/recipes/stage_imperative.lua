-- Test imperative stage function using ctx.extract_all
identity = "local.stage_imperative@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  -- Custom stage logic - extract all with strip
  ctx.extract_all({strip = 1})
end
