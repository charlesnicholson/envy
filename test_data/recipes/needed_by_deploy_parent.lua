-- Tests needed_by="deploy" - dependency completes before parent's deploy phase
IDENTITY = "local.needed_by_deploy_parent@v1"

DEPENDENCIES = {
  { recipe = "local.needed_by_deploy_dep@v1", source = "needed_by_deploy_dep.lua", needed_by = "deploy" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

-- Deploy phase runs after dependency completes
-- (deploy phase is currently a no-op, but the edge exists)
