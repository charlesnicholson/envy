-- Test ctx.run with quiet=true only
identity = "local.check_combo_quiet@v1"

function check(ctx)
    -- Quiet only: no TUI, returns exit_code only
    local res = ctx.run("echo 'quiet test'", {quiet = true})

    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
