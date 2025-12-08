-- Test shell error: command not found
identity = "local.check_error_not_found@v1"

function check(ctx)
    local res = ctx.run("nonexistent_command_12345", {quiet = true, check = true})
    error("Should have thrown on command not found")
end

function install(ctx)
    -- Not reached due to check error
end
