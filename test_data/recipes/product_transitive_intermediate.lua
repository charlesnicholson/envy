-- Middle node that depends on the actual provider
IDENTITY = "local.product_transitive_intermediate@v1"

-- Does NOT provide "tool" directly, but transitively via dependency
DEPENDENCIES = {
    {
        recipe = "local.product_transitive_provider@v1",
        source = "product_transitive_provider.lua",
    }
}

FETCH = {
    source = "test_data/archives/test.tar.gz",
    sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
