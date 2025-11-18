-- Parent recipe whose recipe_fetch depends on a helper finishing all phases.
identity = "local.fetch_dep_blocked@v1"

dependencies = {
  {
    recipe = "local.fetch_dep_helper@v1",
    source = "fetch_dep_helper.lua",
    needed_by = "check",
  },
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end
