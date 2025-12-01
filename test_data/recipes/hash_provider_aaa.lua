-- Provider for aaa_tool
identity = "local.hash_provider_aaa@v1"

products = {
  aaa_tool = "bin/aaa",
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

install = function(ctx)
  ctx.mark_install_complete()
end
