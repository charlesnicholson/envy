-- Dependency validation test: base library (no dependencies)
identity = "local.dep_val_lib@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})
end

build = function(ctx)
  ctx.run([[echo "lib built" > lib.txt]])
end
