-- Test shell error: syntax error
IDENTITY = "local.check_error_syntax@v1"

function CHECK(ctx)
    -- Run a command with shell syntax error
    local res = ctx.run("echo 'unclosed quote", {quiet = true})

    -- Should not reach here because ctx.run throws on error
    error("Should have thrown on syntax error")
end

function INSTALL(ctx)
    -- Not reached due to check error
end
