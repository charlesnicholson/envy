-- Reference-only dependency with no provider anywhere in the graph
identity = "local.weak_missing_ref@v1"
dependencies = {
  { recipe = "local.never_provided" },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

