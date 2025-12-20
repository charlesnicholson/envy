-- Test that check string success is silent (no TUI output)
IDENTITY = "local.check_string_success@v1"

-- String check that succeeds (exits 0)
CHECK = "echo 'test'"

-- Need install verb since we have check
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Not reached since check always passes
end
