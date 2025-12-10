-- Test ctx.run default (no flags): streams, throws on non-zero, returns exit_code
IDENTITY = "local.check_ctx_run_default@v1"

function CHECK(ctx)
    -- Default behavior: streams to TUI, throws on error, returns table with exit_code
    local res = ctx.run("echo 'default test'")

    -- Should get exit_code field
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.exit_code == 0, "exit_code should be 0")

    -- stdout/stderr should not be in table (not captured)
    assert(res.stdout == nil, "stdout not captured by default")
    assert(res.stderr == nil, "stderr not captured by default")

    return true
end

function INSTALL(ctx)
    -- Not reached since check returns true
end
