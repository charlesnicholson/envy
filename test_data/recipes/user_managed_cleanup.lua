-- Test user-managed entry_dir cleanup
IDENTITY = "local.user_cleanup@v1"

function CHECK(ctx)
    local marker = os.getenv("ENVY_TEST_CLEANUP_MARKER")
    if not marker then error("ENVY_TEST_CLEANUP_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local success, res = pcall(function()
        return ctx.run("test -f '" .. marker .. "'", {quiet = true})
    end)

    return success and res.exit_code == 0
end

function INSTALL(ctx)
    local marker = os.getenv("ENVY_TEST_CLEANUP_MARKER")
    if not marker then error("ENVY_TEST_CLEANUP_MARKER not set") end

    -- Create marker to indicate install ran
    ctx.run("touch '" .. marker .. "'")

    -- tmp_dir and work_dir will be cleaned up automatically after this returns
end
