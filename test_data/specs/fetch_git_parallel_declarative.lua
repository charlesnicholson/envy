-- Test recipe with multiple parallel git fetches (declarative)
IDENTITY = "local.fetch_git_parallel_declarative@v1"

-- Declarative fetch with multiple repos triggers parallel fetch
FETCH = {
  {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
  },
  {
    source = "https://github.com/google/re2.git",
    ref = "2024-07-02"
  }
}

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify both repos were actually fetched by checking for .git/HEAD
    -- If fetch failed or didn't happen, these files won't exist and install will fail
    local repos = {"ninja.git", "re2.git"}
    for _, repo in ipairs(repos) do
        local git_head = stage_dir .. "/" .. repo .. "/.git/HEAD"
        local f = io.open(git_head, "r")
        if not f then
            error("Parallel git fetch failed: repo not found: " .. repo)
        end
        f:close()
    end
end
