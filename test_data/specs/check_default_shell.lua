-- Test that manifest default_shell is respected in check string
IDENTITY = "local.check_default_shell@v1"

-- Use default shell (will use system default)
CHECK = "echo 'shell test'"

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check always passes
end
