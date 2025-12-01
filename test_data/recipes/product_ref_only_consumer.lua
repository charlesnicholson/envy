-- Consumer with ref-only product dependency (no recipe/source, unconstrained)
identity = "local.ref_only_consumer@v1"

dependencies = {
  {
    product = "tool",
    -- No recipe, no source - resolves to ANY provider of "tool"
  },
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

install = function(ctx)
  ctx.mark_install_complete()
end
