-- Recipe A with weak reference that creates cycle after resolution
identity = "local.weak_cycle_a@v1"
dependencies = {
  { recipe = "foo" },  -- Weak ref-only, will match weak_cycle_b
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install
end
