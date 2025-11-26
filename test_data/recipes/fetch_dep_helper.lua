-- Helper recipe that must run to completion before parent recipe_fetch.
identity = "local.fetch_dep_helper@v1"

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
