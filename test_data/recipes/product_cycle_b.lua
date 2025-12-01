identity = "local.cycle_b@v1"
products = { tool_b = "bin/b" }

dependencies = {
  {
    product = "tool_a",
    recipe = "local.cycle_a@v1",
    weak = {
      recipe = "local.cycle_a@v1",
      source = "product_cycle_a.lua",
    }
  },
}

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

install = function(ctx)
  ctx.mark_install_complete()
end
