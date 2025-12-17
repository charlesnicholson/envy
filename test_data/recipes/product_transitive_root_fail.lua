-- Root node with weak product dependency that will fail validation
IDENTITY = "local.product_transitive_root_fail@v1"

DEPENDENCIES = {
    {
        product = "tool",
        weak = {
            recipe = "local.product_transitive_intermediate_no_provide@v1",
            source = "product_transitive_intermediate_no_provide.lua",
        }
    }
}

FETCH = {
    source = "test_data/archives/test.tar.gz",
    sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(ctx)
end
