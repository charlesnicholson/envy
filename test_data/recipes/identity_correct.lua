-- Recipe with correct identity declaration (valid)
IDENTITY = "local.identity_correct@v1"
DEPENDENCIES = {}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  envy.info("Identity validation passed")
end
