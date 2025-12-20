-- Recipe B in a fetch dependency cycle: B fetch needs A
IDENTITY = "local.fetch_cycle_b@v1"
DEPENDENCIES = {
  {
    recipe = "local.fetch_cycle_a@v1",
    source = "fetch_cycle_a.lua",  -- Will be fetched by custom fetch function
    fetch = function(tmp_dir, options)
      -- Custom fetch that needs fetch_cycle_a to be available
      -- This completes the cycle: A fetch needs B, B fetch needs A
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
