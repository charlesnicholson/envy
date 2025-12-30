-- Root node with weak product dependency on "tool"
IDENTITY = "local.product_transitive_root@v1"

DEPENDENCIES = {
    {
        product = "tool",
        weak = {
            spec = "local.product_transitive_intermediate@v1",
            source = "product_transitive_intermediate.lua",
        }
    }
}

FETCH = {
    source = "test_data/archives/test.tar.gz",
    sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- If fallback was used, we have intermediate; if provider was in manifest, we don't
    -- Either way, the transitive provision validation should have ensured correctness
end
