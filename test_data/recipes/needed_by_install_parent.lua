-- Tests needed_by="install" - dependency completes before parent's install phase
identity = "local.needed_by_install_parent@v1"

dependencies = {
  { recipe = "local.needed_by_install_dep@v1", source = "needed_by_install_dep.lua", needed_by = "install" }
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

install = function(ctx, opts)
  -- Can access dependency in install phase
  ctx.asset("local.needed_by_install_dep@v1")
  ctx.mark_install_complete()
end
