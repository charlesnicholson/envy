identity = "local.apt@r0"

local missing_packages = {}

check = function(ctx, opts)
  local res = ctx.run("dpkg-query -W -f='${Package}\n'", {
    capture = true,
    quiet = true,
  })

  if res.exit_code ~= 0 then
    return false
  end

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

  for _, pkg in ipairs(missing_packages) do print(pkg) end

  return #missing_packages == 0
end

install = function(ctx, opts)
  if #missing_packages == 0 then
    return nil
  end

  return "sudo apt-get install -y " .. table.concat(missing_packages, " ")
end
