-- Test install cwd = manifest directory for user-managed packages
IDENTITY = "local.install_cwd_user@v1"

-- Has check verb, so this is user-managed
function CHECK(ctx)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local test_cmd = envy.PLATFORM == "windows"
        and ('if (Test-Path \'' .. marker .. '\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function()
        return ctx.run(test_cmd, {quiet = true})
    end)

    return success and res.exit_code == 0
end

function INSTALL(ctx)
    local marker = os.getenv("ENVY_TEST_INSTALL_MARKER")
    if not marker then error("ENVY_TEST_INSTALL_MARKER not set") end

    -- Write a file using relative path to verify cwd
    if envy.PLATFORM == "windows" then
        ctx.run('"user_managed_cwd_test" | Out-File -FilePath user_install_cwd_marker.txt')
    else
        ctx.run("echo 'user_managed_cwd_test' > user_install_cwd_marker.txt")
    end

    -- Verify we can access it with relative path
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path user_install_cwd_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f user_install_cwd_marker.txt"
    local res = ctx.run(test_cmd, {quiet = true})

    -- Clean up
    if envy.PLATFORM == "windows" then
        ctx.run('Remove-Item -Force -ErrorAction SilentlyContinue user_install_cwd_marker.txt', {quiet = true})
    else
        ctx.run("rm -f user_install_cwd_marker.txt", {quiet = true})
    end

    if res.exit_code ~= 0 then
        error("Could not create file with relative path - cwd issue")
    end

    -- Create marker to indicate success
    if envy.PLATFORM == "windows" then
        ctx.run('New-Item -ItemType File -Force -Path \'' .. marker .. '\' | Out-Null')
    else
        ctx.run("touch '" .. marker .. "'")
    end
end
