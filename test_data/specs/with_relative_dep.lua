-- Spec that uses relative path for dependency
IDENTITY = "local.with_relative_dep@v1"
DEPENDENCIES = {
  { spec = "local.simple@v1", source = "./simple.lua" }
}
