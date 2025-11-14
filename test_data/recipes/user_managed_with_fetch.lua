-- User-managed package that uses fetch/stage/build verbs
-- Demonstrates that ephemeral workspace (fetch_dir, stage_dir) gets purged
identity = "local.user_managed_with_fetch@v1"

-- Declarative fetch: download a small file for testing
fetch = {
    source = "https://raw.githubusercontent.com/ninja-build/ninja/v1.11.1/README.md",
    sha256 = "0db9d908de747f6097ee249764e2d5fab4a2be618b05743d6b962e3346867732"
}

function check(ctx)
    -- Check if installation marker exists (simulates system check)
    local marker = os.getenv("HOME") .. "/.envy-test-marker-with-fetch"
    local f = io.open(marker, "r")
    if f then
        f:close()
        return true
    end
    return false
end

function stage(ctx)
    -- Verify fetch happened
    local readme = ctx.fetch_dir .. "/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Fetch did not produce README.md in fetch_dir")
    end
    f:close()

    -- Extract/copy to stage (in this case, just verify)
    -- Stage will be purged after install completes
end

function build(ctx)
    -- Verify stage_dir exists and is accessible
    if not ctx.stage_dir then
        error("stage_dir not available in build phase")
    end
    -- Build dir will be purged after install completes
end

function install(ctx)
    -- Simulate system installation (create marker)
    local marker = os.getenv("HOME") .. "/.envy-test-marker-with-fetch"
    local f = io.open(marker, "w")
    if not f then
        error("Failed to create marker file")
    end
    f:write("installed with fetch/stage/build")
    f:close()

    -- Verify all directories were available during install
    if not ctx.fetch_dir or not ctx.stage_dir or not ctx.install_dir then
        error("Missing directory context in install phase")
    end

    -- No mark_install_complete() - entire entry_dir will be purged
end
