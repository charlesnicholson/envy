-- Test check with ctx.run capture=true returns stdout, stderr, exit_code
identity = "local.check_ctx_run_capture@v1"

function check(ctx)
    -- Capture=true: should get stdout, stderr, exit_code fields
    local res = ctx.run("echo 'stdout text' && echo 'stderr text' >&2", {capture = true})

    -- Verify all three fields exist
    assert(res.stdout ~= nil, "stdout field should exist")
    assert(res.stderr ~= nil, "stderr field should exist")
    assert(res.exit_code ~= nil, "exit_code field should exist")

    -- Verify content
    assert(res.stdout:match("stdout text"), "stdout should contain expected text")
    assert(res.exit_code == 0, "exit_code should be 0")

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
