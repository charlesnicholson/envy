-- Test dependency for build_with_asset
identity = "local.build_dependency@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  if ENVY_PLATFORM == "windows" then
    ctx.run([[Set-Content -Path dependency.txt -Value "dependency_data"]], { shell = "powershell" })
  else
    ctx.run("echo 'dependency_data' > dependency.txt")
  end
end
