-- Test recipe with declarative git fetch
identity = "local.fetch_git@v1"

fetch = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
}

function check(ctx)
    return false
end

function install(ctx)
    -- Verify the fetched git repo is available
    local readme = ctx.fetch_dir .. "/ninja.git/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Could not find README.md in fetched git repo")
    end
    f:close()
end
