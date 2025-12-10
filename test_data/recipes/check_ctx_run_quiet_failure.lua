-- Test check with ctx.run quiet=true on failure (throws)
IDENTITY = "local.check_ctx_run_quiet_fail@v1"

function CHECK(ctx)
    -- Quiet failure: no TUI output, but should throw
    local res = ctx.run("exit 1", {quiet = true})

    -- Should not reach here because ctx.run throws on non-zero
    error("Should have thrown on non-zero exit")
end

function INSTALL(ctx)
    -- Not reached due to check error
end
