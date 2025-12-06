-- Test check with ctx.run capture=false returns only exit_code
identity = "local.check_ctx_run_no_capture@v1"

function check(ctx)
    -- Capture=false (or no capture): should only get exit_code field
    local res = ctx.run("echo 'test'", {capture = false})

    -- Verify only exit_code exists
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.stdout == nil, "stdout field should not exist when capture=false")
    assert(res.stderr == nil, "stderr field should not exist when capture=false")
    assert(res.exit_code == 0, "exit_code should be 0")

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
