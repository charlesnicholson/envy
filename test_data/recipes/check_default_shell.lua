-- Test that manifest default_shell is respected in check string
identity = "local.check_default_shell@v1"

-- Use default shell (will use system default)
check = "echo 'shell test'"

function install(ctx)
    -- Not reached since check always passes
end
