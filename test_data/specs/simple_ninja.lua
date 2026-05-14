IDENTITY = "local.ninja@r0"
USER_MANAGED = true
CHECK = function() return true end
INSTALL = function() end
DEPENDENCIES = {
  { spec = "local.python@r0", source = "simple_python.lua" }
}
