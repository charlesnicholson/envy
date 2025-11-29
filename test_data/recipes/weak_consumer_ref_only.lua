-- Reference-only dependency that is satisfied by an existing provider
identity = "local.weak_consumer_ref_only@v1"
dependencies = {
  { recipe = "local.weak_provider@v1", source = "weak_provider.lua" },
  { recipe = "local.weak_provider" },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

