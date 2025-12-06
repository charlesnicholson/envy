-- Test ctx.run with capture=true only
identity = "local.check_combo_capture@v1"

function check(ctx)
    -- Capture only: streams to TUI, returns stdout/stderr/exit_code
    local res = ctx.run("echo 'capture test'", {capture = true})

    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("capture test"))

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
