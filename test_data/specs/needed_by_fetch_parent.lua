-- Tests needed_by="fetch" - dependency completes before parent's fetch phase
IDENTITY = "local.needed_by_fetch_parent@v1"

DEPENDENCIES = {
  { spec = "local.needed_by_fetch_dep@v1", source = "needed_by_fetch_dep.lua", needed_by = "fetch" }
}

FETCH = function(tmp_dir, options)
  -- Can access dependency in fetch phase
  local dep_path = envy.package("local.needed_by_fetch_dep@v1")
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})
end
