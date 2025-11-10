-- Test build phase: access to fetch_dir, stage_dir, install_dir
identity = "local.build_access_dirs@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing directory access")
  print("fetch_dir: " .. ctx.fetch_dir)
  print("stage_dir: " .. ctx.stage_dir)
  print("install_dir: " .. ctx.install_dir)

  -- Verify directories exist
  ctx.run([[
    test -d "]] .. ctx.fetch_dir .. [[" || exit 1
    test -d "]] .. ctx.stage_dir .. [[" || exit 1
    test -d "]] .. ctx.install_dir .. [[" || exit 1
    echo "All directories exist"
  ]])

  -- Verify fetch_dir contains the archive
  ctx.run([[
    test -f "]] .. ctx.fetch_dir .. [[/test.tar.gz" || exit 1
    echo "Archive found in fetch_dir"
  ]])

  -- Create output in install_dir for later verification
  ctx.run([[
    echo "Built successfully" > "]] .. ctx.install_dir .. [[/build_marker.txt"
  ]])

  print("Directory access successful")
end
