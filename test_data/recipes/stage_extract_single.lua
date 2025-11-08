-- Test imperative stage using ctx.extract for single file
identity = "local.stage_extract_single@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef917a6d5daf41890d08b9cfa97b0d2161cab897635cc617c195fcdc4df1730c"
}

stage = function(ctx)
  -- Extract single archive with specific options
  local files = ctx.extract("test.tar.gz", {strip = 1})
  -- files should be 5 (the number of regular files extracted)
end
