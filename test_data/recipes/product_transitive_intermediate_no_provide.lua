-- Middle node that does NOT provide "tool" and has no dependencies
-- Used to test validation failure when fallback doesn't transitively provide
IDENTITY = "local.product_transitive_intermediate_no_provide@v1"

-- No PRODUCTS, no DEPENDENCIES - cannot provide "tool" transitively

FETCH = {
    source = "test_data/archives/test.tar.gz",
    sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(ctx)
    ctx.mark_install_complete()
end
