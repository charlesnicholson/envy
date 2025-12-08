identity = "local.apt@r0"

local missing_packages = {}

check = function(ctx, opts)
  local cmd = "dpkg-query -W -f='${Package}\n' " .. table.concat(opts.packages, " ")
  local res = ctx.run(cmd, { capture = true, quiet = true })

  local installed = {}

  for line in res.stdout:gmatch("[^\r\n]+") do
    local pkg = line:match("^%S+")
    if pkg then
      installed[pkg] = true
    end
  end

  missing_packages = {}

  for _, pkg in ipairs(opts.packages) do
    if not installed[pkg] then
      table.insert(missing_packages, pkg)
    end
  end

  return #missing_packages == 0
end

install = function(ctx, opts)
  if #missing_packages == 0 then
    return nil
  end

  return "sudo apt-get install -y " .. table.concat(missing_packages, " ")
end
