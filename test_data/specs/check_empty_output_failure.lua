-- Test empty outputs in failure messages
IDENTITY = "local.check_empty_output@v1"

function CHECK(project_root, options)
    -- Run command that fails with no output
    local res = envy.run("exit 42", {quiet = true})

    -- Should not reach here
    error("Should have thrown on non-zero exit")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
