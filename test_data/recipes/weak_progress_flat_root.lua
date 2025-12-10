-- Two weak fallbacks that each introduce new weak references; unresolved count stays flat
IDENTITY = "local.weak_progress_flat_root@v1"
DEPENDENCIES = {
  { recipe = "local.branch_one", weak = { recipe = "local.branch_one@v1", source = "weak_branch_one.lua" } },
  { recipe = "local.branch_two", weak = { recipe = "local.branch_two@v1", source = "weak_branch_two.lua" } },
  { recipe = "local.shared" },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end

