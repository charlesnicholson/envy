-- Test ctx.run with quiet=true only
IDENTITY = "local.check_combo_quiet@v1"

function CHECK(project_root, options)
    -- Quiet only: no TUI, returns exit_code only
    local res = envy.run("echo 'quiet test'", {quiet = true})

    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
