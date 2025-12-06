-- Test ctx.run with neither quiet nor capture
identity = "local.check_combo_neither@v1"

function check(ctx)
    -- Neither quiet nor capture: streams, returns exit_code only
    local res = ctx.run("echo 'combo test'")

    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
