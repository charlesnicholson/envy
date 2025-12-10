-- Test empty outputs in failure messages
IDENTITY = "local.check_empty_output@v1"

function CHECK(ctx)
    -- Run command that fails with no output
    local res = ctx.run("exit 42", {quiet = true})

    -- Should not reach here
    error("Should have thrown on non-zero exit")
end

function INSTALL(ctx)
    -- Not reached due to check error
end
