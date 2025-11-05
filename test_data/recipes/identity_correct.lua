-- Recipe with correct identity declaration (valid)
identity = "local.identity_correct@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  envy.info("Identity validation passed")
end
