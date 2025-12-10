-- Test shell error: command not found
IDENTITY = "local.check_error_not_found@v1"

function CHECK(ctx)
    local res = ctx.run("nonexistent_command_12345", {quiet = true, check = true})
    error("Should have thrown on command not found")
end

function INSTALL(ctx)
    -- Not reached due to check error
end
