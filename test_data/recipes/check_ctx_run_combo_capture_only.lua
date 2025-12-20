-- Test ctx.run with capture=true only
IDENTITY = "local.check_combo_capture@v1"

function CHECK(project_root, options)
    -- Capture only: streams to TUI, returns stdout/stderr/exit_code
    local res = envy.run("echo 'capture test'", {capture = true})

    assert(res.exit_code == 0)
    assert(res.stdout ~= nil, "stdout should be captured")
    assert(res.stderr ~= nil, "stderr should be captured")
    assert(res.stdout:match("capture test"))

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
