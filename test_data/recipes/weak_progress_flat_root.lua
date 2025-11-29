-- Two weak fallbacks that each introduce new weak references; unresolved count stays flat
identity = "local.weak_progress_flat_root@v1"
dependencies = {
  { recipe = "local.branch_one", weak = { recipe = "local.branch_one@v1", source = "weak_branch_one.lua" } },
  { recipe = "local.branch_two", weak = { recipe = "local.branch_two@v1", source = "weak_branch_two.lua" } },
  { recipe = "local.shared" },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

