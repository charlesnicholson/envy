identity = "local.brew_package@r0"

dependencies = { recipe = "local.brew@r0", source = "local.brew@r0.lua" }

check = "which ghostty"
install = function(ctx, opts) return "brew install " .. opts.package end
