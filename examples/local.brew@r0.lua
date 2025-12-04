identity = "local.brew@r0"

check = function(ctx)
  return ctx.run("brew --version")
end

install = function(ctx)
  print("installing brew")
end
