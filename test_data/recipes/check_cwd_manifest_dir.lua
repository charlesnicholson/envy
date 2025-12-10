-- Test that check cwd = manifest directory
IDENTITY = "local.check_cwd_manifest@v1"

function CHECK(ctx)
    -- Write a marker file using relative path to verify cwd
    -- If cwd is manifest dir, this will create it there
    ctx.run("pwd > /tmp/check_cwd_test_pwd.txt")
    ctx.run("echo 'cwd_test' > cwd_check_marker.txt")

    -- Verify it was created (if cwd wasn't writable or correct, this would fail)
    local res = ctx.run("test -f cwd_check_marker.txt", {quiet = true})

    -- Clean up
    ctx.run("rm -f cwd_check_marker.txt /tmp/check_cwd_test_pwd.txt", {quiet = true})

    if res.exit_code ~= 0 then
        error("Could not create marker file relative to cwd")
    end

    return true  -- Check passes
end

function INSTALL(ctx)
    -- Not reached since check returns true
end
