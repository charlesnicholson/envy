-- Test that manifest default_shell is respected in check string
IDENTITY = "local.check_default_shell@v1"

-- Use default shell (will use system default)
CHECK = "echo 'shell test'"

function INSTALL(ctx)
    -- Not reached since check always passes
end
