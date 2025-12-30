-- Test shell error: command not found
IDENTITY = "local.check_error_not_found@v1"

function CHECK(project_root, options)
    local res = envy.run("nonexistent_command_12345", {quiet = true, check = true})
    error("Should have thrown on command not found")
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached due to check error
end
