-- Cache-managed package that uses fetch/stage/build verbs
-- Demonstrates that workspace (fetch_dir, stage_dir) gets populated and used
IDENTITY = "local.user_managed_with_fetch@v1"

-- Declarative fetch: download a small file for testing
FETCH = {
    source = "https://raw.githubusercontent.com/ninja-build/ninja/v1.13.2/README.md",
    sha256 = "b31e9700c752fa214773c1b799d90efcbf3330c8062da9f45c6064e023b347b0"
}

-- No check verb - this is cache-managed
function STAGE(fetch_dir, stage_dir, tmp_dir, options)
    -- Verify fetch happened
    local readme = fetch_dir .. "/README.md"
    local f = io.open(readme, "r")
    if not f then
        error("Fetch did not produce README.md in fetch_dir")
    end
    f:close()

    -- Extract/copy to stage (in this case, just verify)
    -- Stage will be purged after install completes
end

function BUILD(stage_dir, install_dir, fetch_dir, tmp_dir, options)
    -- Verify stage_dir exists and is accessible
    if not stage_dir then
        error("stage_dir not available in build phase")
    end
    -- Build dir will be purged after install completes
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Simulate system installation (create marker)
    local marker = os.getenv("ENVY_TEST_MARKER_WITH_FETCH")
    if not marker then
        error("ENVY_TEST_MARKER_WITH_FETCH must be set")
    end

    local f = io.open(marker, "w")
    if not f then
        error("Failed to create marker file: " .. marker)
    end
    f:write("installed with fetch/stage/build")
    f:close()

    -- Verify all directories were available during install
    if not fetch_dir or not stage_dir or not install_dir then
        error("Missing directory context in install phase")
    end

    -- Mark install complete for cache-managed package
end
