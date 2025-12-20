IDENTITY = "local.apt@r0"

local missing_packages = {}

CHECK = function(tmp_dir, opts)
  local cmd = "dpkg-query -W -f='${Package}\n' " .. table.concat(opts.packages, " ")
  local res = envy.run(cmd, { capture = true, quiet = true })

  local installed = {}

  for line in res.stdout:gmatch("[^\r\n]+") do
    local pkg = line:match("^%S+")
    if pkg then
      installed[pkg] = true
    end
  end

  missing_packages = {}

  for _, pkg in pairs(opts.packages) do
    if not installed[pkg] then
      table.insert(missing_packages, pkg)
    end
  end

  return #missing_packages == 0
end

INSTALL = function(install_dir, stage_dir, fetch_dir, tmp_dir, opts)
  return "sudo apt-get install -y " .. table.concat(missing_packages, " ")
end
