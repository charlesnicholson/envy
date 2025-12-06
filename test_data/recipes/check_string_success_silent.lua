-- Test that check string success is silent (no TUI output)
identity = "local.check_string_success@v1"

-- String check that succeeds (exits 0)
check = "echo 'test'"

-- Need install verb since we have check
function install(ctx)
    -- Not reached since check always passes
end
