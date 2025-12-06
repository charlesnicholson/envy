-- Test empty outputs in failure messages
identity = "local.check_empty_output@v1"

function check(ctx)
    -- Run command that fails with no output
    local res = ctx.run("exit 42", {quiet = true})

    -- Should not reach here
    error("Should have thrown on non-zero exit")
end

function install(ctx)
    -- Not reached due to check error
end
