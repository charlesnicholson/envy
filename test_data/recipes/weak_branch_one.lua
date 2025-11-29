-- Fallback that creates a shared dependency
identity = "local.branch_one@v1"
dependencies = {
  { recipe = "local.shared", weak = { recipe = "local.shared@v1", source = "weak_shared.lua" } },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

