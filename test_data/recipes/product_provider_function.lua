-- Provider with programmatic products function (takes options, returns table)
IDENTITY = "local.product_function@v1"

PRODUCTS = function(options)
  return {
    ["python" .. options.version] = "bin/python",
    ["pip" .. options.version] = "bin/pip",
  }
end

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c",
}

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, options)
end
