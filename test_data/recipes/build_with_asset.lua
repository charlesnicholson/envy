-- Test build phase: ctx.asset() for dependency access
identity = "local.build_with_asset@v1"

dependencies = {
  { recipe = "local.build_dependency@v1", file = "build_dependency.lua" }
}

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Accessing dependency via ctx.asset()")

  local dep_path = ctx.asset("local.build_dependency@v1")
  print("Dependency path: " .. dep_path)

  -- Copy dependency file
  local result
  if ENVY_PLATFORM == "windows" then
    result = ctx.run([[
      Get-Content "]] .. dep_path .. [[/dependency.txt" | Set-Content -Path from_dependency.txt
      Write-Output "Used dependency data"
    ]], { shell = "powershell" })
  else
    result = ctx.run([[
      cat "]] .. dep_path .. [[/dependency.txt" > from_dependency.txt
      echo "Used dependency data"
    ]])
  end

  if not result.stdout:match("Used dependency data") then
    error("Failed to use dependency")
  end
end
