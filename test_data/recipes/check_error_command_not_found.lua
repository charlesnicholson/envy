-- Test shell error: command not found
identity = "local.check_error_not_found@v1"

function check(ctx)
    -- Try to run a command that doesn't exist
    local res = ctx.run("nonexistent_command_12345", {quiet = true})

    -- Should not reach here because ctx.run throws on error
    error("Should have thrown on command not found")
end

function install(ctx)
    -- Not reached due to check error
end
