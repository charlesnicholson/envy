-- Test imperative stage using ctx.extract for single file
identity = "local.stage_extract_single@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  -- Extract single archive with specific options
  local files = ctx.extract("test.tar.gz", {strip = 1})
  -- files should be 5 (the number of regular files extracted)
end
