-- Second fallback that depends on the same shared target
IDENTITY = "local.branch_two@v1"
DEPENDENCIES = {
  { recipe = "local.shared", weak = { recipe = "local.shared@v1", source = "weak_shared.lua" } },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end

