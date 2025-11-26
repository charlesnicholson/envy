-- Tests needed_by="stage" - dependency completes before parent's stage phase
identity = "local.needed_by_stage_parent@v1"

dependencies = {
  { recipe = "local.needed_by_stage_dep@v1", source = "needed_by_stage_dep.lua", needed_by = "stage" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Can access dependency in stage phase
  ctx.asset("local.needed_by_stage_dep@v1")
end
