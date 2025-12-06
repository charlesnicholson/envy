-- Test install cwd = manifest directory for user-managed packages
identity = "local.install_cwd_user@v1"

-- Has check verb, so this is user-managed
function check(ctx)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local success, res = pcall(function()
        return ctx.run("test -f '" .. marker .. "'", {quiet = true})
    end)

    return success and res.exit_code == 0
end

function install(ctx)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end

    -- Write a file using relative path to verify cwd
    ctx.run("echo 'user_managed_cwd_test' > user_install_cwd_marker.txt")

    -- Verify we can access it with relative path
    local res = ctx.run("test -f user_install_cwd_marker.txt", {quiet = true})

    -- Clean up
    ctx.run("rm -f user_install_cwd_marker.txt", {quiet = true})

    if res.exit_code ~= 0 then
        error("Could not create file with relative path - cwd issue")
    end

    -- Create marker to indicate success
    ctx.run("touch '" .. marker .. "'")
end
