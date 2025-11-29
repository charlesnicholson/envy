-- Recipe A in a fetch dependency cycle: A fetch needs B
identity = "local.fetch_cycle_a@v1"
dependencies = {
  {
    recipe = "local.fetch_cycle_b@v1",
    source = "fetch_cycle_b.lua",  -- Will be fetched by custom fetch function
    fetch = function(ctx, opts)
      -- Custom fetch that needs fetch_cycle_b to be available
      -- This creates a cycle since fetch_cycle_b also fetch-depends on A
      error("Should not reach here - cycle should be detected first")
    end
  }
}

function fetch(ctx)
  -- Simple fetch function for this recipe
end

function install(ctx)
  -- Programmatic package
end
