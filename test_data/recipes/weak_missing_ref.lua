-- Reference-only dependency with no provider anywhere in the graph
IDENTITY = "local.weak_missing_ref@v1"
DEPENDENCIES = {
  { recipe = "local.never_provided" },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end

