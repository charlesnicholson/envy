-- Test shell error: syntax error
IDENTITY = "local.check_error_syntax@v1"

function CHECK(project_root, options)
    -- Run a command with shell syntax error
    local res = envy.run("echo 'unclosed quote", {quiet = true})

    -- Should not reach here because ctx.run throws on error
    error("Should have thrown on syntax error")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
