-- Recipe with identity as wrong type (table instead of string)
identity = { name = "wrong" }
dependencies = {}

function check(ctx)
  return false
end

function install(ctx)
  -- This should never execute
end
