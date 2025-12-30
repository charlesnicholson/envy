-- Recipe A in a fetch dependency cycle: A fetch needs B
IDENTITY = "local.fetch_cycle_a@v1"
DEPENDENCIES = {
  {
    spec = "local.fetch_cycle_b@v1",
    source = "fetch_cycle_b.lua",  -- Will be fetched by custom fetch function
    fetch = function(tmp_dir, options)
      -- Custom fetch that needs fetch_cycle_b to be available
      -- This creates a cycle since fetch_cycle_b also fetch-depends on A
      error("Should not reach here - cycle should be detected first")
    end
  }
}

function FETCH(tmp_dir, options)
  -- Simple fetch function for this recipe
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
  -- Programmatic package
end
