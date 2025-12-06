identity = "local.brew_package@r0"

dependencies = { recipe = "local.brew@r0", source = "local.brew@r0.lua" }

check = function(ctx, opts)
  local res = ctx.run("brew list", {capture=true, quiet=true})
  if res.exit_code ~= 0 then
    return false
  end

  return res.stdout:find(opts.package, 1, true) ~= nil
end

install = function(ctx, opts)
  return "brew install " .. opts.package
end
