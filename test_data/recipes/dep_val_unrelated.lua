-- Test for unrelated recipe error
-- This recipe tries to access lib without declaring it
identity = "local.dep_val_unrelated@v1"

-- Intentionally NOT declaring any dependencies

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
  -- Try to access lib without declaring it - should fail
  ctx.asset("local.dep_val_lib@v1")
end
