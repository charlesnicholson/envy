-- Test check with ctx.run quiet=true on success
IDENTITY = "local.check_ctx_run_quiet@v1"

function CHECK(project_root, options)
    -- Quiet success: no TUI output, returns table with exit_code
    local res = envy.run("echo 'test output'", {quiet = true})

    -- Verify exit_code field exists
    assert(res.exit_code ~= nil, "exit_code field should exist")
    assert(res.exit_code == 0, "exit_code should be 0")

    return true  -- Check passes, skip install
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
