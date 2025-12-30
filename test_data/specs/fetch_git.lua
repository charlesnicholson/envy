-- Test recipe with declarative git fetch
IDENTITY = "local.fetch_git@v1"

FETCH = {
    source = "https://github.com/ninja-build/ninja.git",
    ref = "v1.11.1"
}

function CHECK(project_root, options)
    return false
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify the fetched git repo is available in stage_dir/ninja.git/
    envy.ls(stage_dir)
    local readme = stage_dir .. "/ninja.git/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Could not find README.md at: " .. readme)
    end
    f:close()

    -- Verify .git directory is present (kept for packages that need it)
    -- Check by opening a file that must exist in a git repo
    local git_head = stage_dir .. "/ninja.git/.git/HEAD"
    local g = io.open(git_head, "r")
    if not g then
        error(".git directory should be present at: " .. stage_dir .. "/ninja.git/.git (tried to open HEAD file)")
    end
    g:close()
end
