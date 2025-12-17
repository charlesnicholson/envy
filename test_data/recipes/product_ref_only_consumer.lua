-- Consumer with ref-only product dependency (no recipe/source, unconstrained)
IDENTITY = "local.ref_only_consumer@v1"

DEPENDENCIES = {
  {
    product = "tool",
    -- No recipe, no source - resolves to ANY provider of "tool"
  },
}

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(ctx)
end
