-- Tests explicit needed_by="fetch" - dependency completes before parent's fetch phase
identity = "local.explicit_fetch_parent@v1"

dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua", needed_by = "fetch" }
}

fetch = function(ctx)
  -- Dependency will complete before this phase due to explicit needed_by
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx)
  ctx.extract_all({strip = 1})
end
