-- Test imperative stage function using ctx.extract_all
identity = "local.stage_imperative@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef917a6d5daf41890d08b9cfa97b0d2161cab897635cc617c195fcdc4df1730c"
}

stage = function(ctx)
  -- Custom stage logic - extract all with strip
  ctx.extract_all({strip = 1})
end
