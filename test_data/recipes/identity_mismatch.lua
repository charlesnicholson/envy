-- Recipe with wrong identity declaration (mismatch)
identity = "local.wrong_identity@v1"
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- This should never execute
end
