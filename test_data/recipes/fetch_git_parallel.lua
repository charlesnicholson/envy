-- Test recipe with multiple parallel git fetches (programmatic)
identity = "local.fetch_git_parallel@v1"

function fetch(ctx)
    -- Programmatic fetch with multiple git repos (should parallelize)
    ctx.fetch({
        {source = "https://github.com/ninja-build/ninja.git", ref = "v1.11.1"},
        {source = "https://github.com/google/re2.git", ref = "2024-07-02"}
    })
end

function check(ctx)
    return false
end

function install(ctx)
    -- Verify both repos were actually fetched by checking for .git/HEAD
    -- If fetch failed or didn't happen, these files won't exist and install will fail
    local repos = {"ninja.git", "re2.git"}
    for _, repo in ipairs(repos) do
        local git_head = ctx.stage_dir .. "/" .. repo .. "/.git/HEAD"
        local f = io.open(git_head, "r")
        if not f then
            error("Parallel git fetch failed: repo not found: " .. repo)
        end
        f:close()
    end
end
