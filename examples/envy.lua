packages = {
  { recipe = "local.armgcc@r0", source = "local.armgcc@r0.lua" },

  { recipe = "local.python@r0",
    source = "local.python@r0.lua",
    options = { version = "3.13.9", provide_python3 = true } },

  { recipe = "local.python@r0",
    source = "local.python@r0.lua",
    options = { version = "3.14.0"} },

  { recipe = "local.ninja@r0", source = "local.ninja@r0.lua" },
  { recipe = "local.gn@r0", source = "local.gn@r0.lua" },
  { recipe = "local.uv@r0", source = "local.uv@r0.lua" },
}

