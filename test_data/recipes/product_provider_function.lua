-- Provider with programmatic products function (takes options, returns table)
identity = "local.product_function@v1"

products = function(options)
  return {
    ["python" .. options.version] = "bin/python",
    ["pip" .. options.version] = "bin/pip",
  }
end

fetch = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

install = function(ctx)
  ctx.mark_install_complete()
end
