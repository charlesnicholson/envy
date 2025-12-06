-- Test ctx.run with both quiet=true and capture=true
identity = "local.check_combo_both@v1"

function check(ctx)
    -- Both quiet and capture: no TUI, returns stdout/stderr/exit_code
    local res = ctx.run("echo 'both test'", {quiet = true, capture = true})

    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("both test"))

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
