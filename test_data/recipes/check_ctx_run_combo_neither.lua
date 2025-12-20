-- Test ctx.run with neither quiet nor capture
IDENTITY = "local.check_combo_neither@v1"

function CHECK(project_root, options)
    -- Neither quiet nor capture: streams, returns exit_code only
    local res = envy.run("echo 'combo test'")

    assert(res.exit_code == 0)
    assert(res.stdout == nil, "stdout not captured")
    assert(res.stderr == nil, "stderr not captured")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
