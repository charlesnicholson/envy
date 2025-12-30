-- Test direct table field access patterns
IDENTITY = "local.check_table_fields@v1"

function CHECK(project_root, options)
    -- Test direct field access
    local res = envy.run("echo 'test output'", {capture = true})

    -- Access via variable
    local code = res.exit_code
    local out = res.stdout
    local err = res.stderr

    assert(code == 0, "exit_code should be 0")
    assert(out ~= nil, "stdout should exist")
    assert(err ~= nil, "stderr should exist")
    assert(out:match("test output"), "stdout should contain expected text")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
