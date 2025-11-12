-- Recipe that uses relative path for dependency
identity = "local.with_relative_dep@v1"
dependencies = {
  { recipe = "local.simple@v1", source = "./simple.lua" }
}
