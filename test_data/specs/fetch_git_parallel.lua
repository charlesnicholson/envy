-- Test spec with multiple parallel git fetches (programmatic)
IDENTITY = "local.fetch_git_parallel@v1"

function FETCH(tmp_dir, options)
    -- Programmatic fetch with multiple git repos (should parallelize)
    envy.fetch({
        {source = "https://github.com/ninja-build/ninja.git", ref = "v1.13.2"},
        {source = "https://github.com/google/re2.git", ref = "2024-07-02"}
    }, {dest = tmp_dir})
    envy.commit_fetch({"ninja.git", "re2.git"})
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify both repos were actually fetched by checking for .git/HEAD
    -- If fetch failed or didn't happen, these files won't exist and install will fail
    local repos = {"ninja.git", "re2.git"}
    for _, repo in ipairs(repos) do
        local git_head = fetch_dir .. "/" .. repo .. "/.git/HEAD"
        local f = io.open(git_head, "r")
        if not f then
            error("Parallel git fetch failed: repo not found: " .. repo)
        end
        f:close()
    end
end
