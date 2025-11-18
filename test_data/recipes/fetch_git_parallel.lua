-- Test recipe with multiple parallel git fetches
identity = "local.fetch_git_parallel@v1"

-- Multiple git repos to fetch concurrently
fetch = {
  {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
  },
  {
    source = "https://github.com/madler/zlib.git",
    ref = "v1.2.13"
  },
  {
    source = "https://github.com/glennrp/libpng.git",
    ref = "v1.6.39"
  }
}

function check(ctx)
    return false
end

function install(ctx)
    -- Verify all three repos were fetched
    local repos = {"ninja.git", "zlib.git", "libpng.git"}
    for _, repo in ipairs(repos) do
        local readme_paths = {
            ctx.stage_dir .. "/" .. repo .. "/README.md",
            ctx.stage_dir .. "/" .. repo .. "/README",
            ctx.stage_dir .. "/" .. repo .. "/README.txt"
        }
        local found = false
        for _, readme in ipairs(readme_paths) do
            local f = io.open(readme, "r")
            if f then
                f:close()
                found = true
                break
            end
        end
        if not found then
            error("Could not find README in: " .. ctx.stage_dir .. "/" .. repo)
        end
    end
end
