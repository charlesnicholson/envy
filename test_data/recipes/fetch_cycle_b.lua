-- Recipe B in a fetch dependency cycle: B fetch needs A
identity = "local.fetch_cycle_b@v1"
dependencies = {
  {
    recipe = "local.fetch_cycle_a@v1",
    source = "fetch_cycle_a.lua",  -- Will be fetched by custom fetch function
    fetch = function(ctx, opts)
      -- Custom fetch that needs fetch_cycle_a to be available
      -- This completes the cycle: A fetch needs B, B fetch needs A
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
