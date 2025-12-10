-- Test that check string success is silent (no TUI output)
IDENTITY = "local.check_string_success@v1"

-- String check that succeeds (exits 0)
CHECK = "echo 'test'"

-- Need install verb since we have check
function INSTALL(ctx)
    -- Not reached since check always passes
end
