-- Base recipe that will be a fetch dependency for another recipe
IDENTITY = "local.simple_fetch_dep_base@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
