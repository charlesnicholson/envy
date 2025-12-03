-- Tests explicit needed_by="fetch" - dependency completes before parent's fetch phase
identity = "local.explicit_fetch_parent@v1"

dependencies = {
  { recipe = "local.dep_val_lib@v1", source = "dep_val_lib.lua", needed_by = "fetch" }
}

fetch = function(ctx, opts)
  -- Dependency will complete before this phase due to explicit needed_by
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
