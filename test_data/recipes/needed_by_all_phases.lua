-- Tests recipe with multiple dependencies using different needed_by phases
IDENTITY = "local.needed_by_all_phases@v1"

DEPENDENCIES = {
  { recipe = "local.needed_by_fetch_dep@v1", source = "needed_by_fetch_dep.lua", needed_by = "fetch" },
  { recipe = "local.needed_by_check_dep@v1", source = "needed_by_check_dep.lua", needed_by = "check" },
  { recipe = "local.needed_by_stage_dep@v1", source = "needed_by_stage_dep.lua", needed_by = "stage" },
  { recipe = "local.needed_by_build_dep@v1", source = "needed_by_build_dep.lua", needed_by = "build" },
  { recipe = "local.needed_by_install_dep@v1", source = "needed_by_install_dep.lua", needed_by = "install" }
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

BUILD = function(ctx, opts)
  -- Access all dependencies
  ctx.asset("local.needed_by_fetch_dep@v1")
  ctx.asset("local.needed_by_check_dep@v1")
  ctx.asset("local.needed_by_stage_dep@v1")
  ctx.asset("local.needed_by_build_dep@v1")
end

INSTALL = function(ctx, opts)
  ctx.asset("local.needed_by_install_dep@v1")
  ctx.mark_install_complete()
end
