-- Provider for zzz_tool
identity = "local.hash_provider_zzz@v1"

products = {
  zzz_tool = "bin/zzz",
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

install = function(ctx)
  ctx.mark_install_complete()
end
