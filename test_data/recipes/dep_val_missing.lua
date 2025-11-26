-- Dependency validation test: NEGATIVE - calls ctx.asset without declaring dependency
identity = "local.dep_val_missing@v1"

-- Note: NO dependencies declared, but we try to access dep_val_lib below

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx, opts)
  ctx.extract_all({strip = 1})
end

build = function(ctx, opts)
  -- Try to access lib without declaring it - SHOULD FAIL
  local lib_path = ctx.asset("local.dep_val_lib@v1")
  ctx.run([[echo "should not get here" > bad.txt]])
end
