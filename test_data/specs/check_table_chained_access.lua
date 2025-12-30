-- Test chained table field access patterns
IDENTITY = "local.check_table_chained@v1"

function CHECK(project_root, options)
    -- Test chained access: envy.run(...).field
    local out = envy.run("echo 'chained'", {capture = true}).stdout
    assert(out:match("chained"), "chained stdout access should work")

    local code = envy.run("echo 'test'", {quiet = true}).exit_code
    assert(code == 0, "chained exit_code access should work")

    return true
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check returns true
end
