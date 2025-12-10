-- Reference-only dependency that matches multiple strong candidates
IDENTITY = "local.weak_consumer_ambiguous@v1"
DEPENDENCIES = {
  { recipe = "local.dupe@v1", source = "weak_dupe_v1.lua" },
  { recipe = "local.dupe@v2", source = "weak_dupe_v2.lua" },
  { recipe = "local.dupe" },
}

function CHECK(ctx)
  return false
end

function INSTALL(ctx)
  -- Programmatic install: no cache artifacts
end

