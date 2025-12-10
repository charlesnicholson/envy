-- Tests needed_by="check" - dependency completes before parent's check phase
IDENTITY = "local.needed_by_check_parent@v1"

DEPENDENCIES = {
  { recipe = "local.needed_by_check_dep@v1", source = "needed_by_check_dep.lua", needed_by = "check" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Dependency is available by now since check already completed
  ctx.asset("local.needed_by_check_dep@v1")
end
