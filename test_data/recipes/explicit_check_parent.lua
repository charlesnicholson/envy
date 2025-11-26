-- Tests explicit needed_by="check" - dependency completes before parent's check phase
identity = "local.explicit_check_parent@v1"

dependencies = {
  { recipe = "local.simple@v1", source = "simple.lua", needed_by = "check" }
}

fetch = function(ctx, opts)
  -- Dependency will complete by check phase (before fetch)
  return "test_data/archives/test.tar.gz", "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
end

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end
