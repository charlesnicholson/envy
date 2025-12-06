-- Test concurrent large stdout+stderr with capture (no deadlock)
identity = "local.check_concurrent@v1"

function check(ctx)
    -- Generate large output on both stdout and stderr
    local cmd = [[
for i in $(seq 1 1000); do
    echo "stdout line $i"
    echo "stderr line $i" >&2
done
]]

    local res = ctx.run(cmd, {capture = true})

    -- Verify we got output and didn't deadlock
    assert(res.exit_code == 0, "command should succeed")
    assert(res.stdout:match("stdout line"), "should have stdout")
    assert(res.stderr:match("stderr line"), "should have stderr")

    return true
end

function install(ctx)
    -- Not reached since check returns true
end
