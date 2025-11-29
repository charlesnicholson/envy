-- Reference-only dependency that matches multiple strong candidates
identity = "local.weak_consumer_ambiguous@v1"
dependencies = {
  { recipe = "local.dupe@v1", source = "weak_dupe_v1.lua" },
  { recipe = "local.dupe@v2", source = "weak_dupe_v2.lua" },
  { recipe = "local.dupe" },
}

function check(ctx)
  return false
end

function install(ctx)
  -- Programmatic install: no cache artifacts
end

