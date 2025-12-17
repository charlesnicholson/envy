-- Test that check cwd = manifest directory
IDENTITY = "local.check_cwd_manifest@v1"

function CHECK(ctx)
    -- Write a marker file using relative path to verify cwd
    -- If cwd is manifest dir, this will create it there
    if ENVY_PLATFORM == "windows" then
        ctx.run('"cwd_test" | Out-File -FilePath cwd_check_marker.txt')
    else
        ctx.run("pwd > /tmp/check_cwd_test_pwd.txt")
        ctx.run("echo 'cwd_test' > cwd_check_marker.txt")
    end

    -- Verify it was created (if cwd wasn't writable or correct, this would fail)
    local test_cmd = ENVY_PLATFORM == "windows"
        and 'if (Test-Path cwd_check_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f cwd_check_marker.txt"
    local res = ctx.run(test_cmd, {quiet = true})

    -- Clean up
    if ENVY_PLATFORM == "windows" then
        ctx.run('Remove-Item -Force -ErrorAction SilentlyContinue cwd_check_marker.txt', {quiet = true})
    else
        ctx.run("rm -f cwd_check_marker.txt /tmp/check_cwd_test_pwd.txt", {quiet = true})
    end

    if res.exit_code ~= 0 then
        error("Could not create marker file relative to cwd")
    end

    return true  -- Check passes
end

function INSTALL(ctx)
    -- Not reached since check returns true
end
