-- Tests needed_by="fetch" - dependency completes before parent's fetch phase
identity = "local.needed_by_fetch_parent@v1"

dependencies = {
  { recipe = "local.needed_by_fetch_dep@v1", source = "needed_by_fetch_dep.lua", needed_by = "fetch" }
}

fetch = function(ctx)
  -- Can access dependency in fetch phase
  local dep_path = ctx.asset("local.needed_by_fetch_dep@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx)
  ctx.extract_all({strip = 1})
end
