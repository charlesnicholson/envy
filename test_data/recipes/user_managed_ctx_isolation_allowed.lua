-- Test that user-managed packages CAN access allowed ctx APIs
identity = "local.user_managed_ctx_isolation_allowed@v1"

function check(ctx)
    return false  -- Always needs install
end

function install(ctx)
    -- Verify identity is accessible
    if not ctx.identity then
        error("ctx.identity should be accessible")
    end
    if ctx.identity ~= "local.user_managed_ctx_isolation_allowed@v1" then
        error("ctx.identity mismatch: " .. ctx.identity)
    end

    -- Verify run() is accessible
    if not ctx.run then
        error("ctx.run should be accessible")
    end
    if type(ctx.run) ~= "function" then
        error("ctx.run should be a function")
    end

    -- Actually call ctx.run to verify it works
    local result = ctx.run("echo test", {capture = true, quiet = true})
    if result.exit_code ~= 0 then
        error("ctx.run failed: " .. result.exit_code)
    end
    if not result.stdout:find("test") then
        error("ctx.run stdout missing 'test': " .. result.stdout)
    end

    -- Verify asset() is accessible (even if we don't have dependencies)
    if not ctx.asset then
        error("ctx.asset should be accessible")
    end
    if type(ctx.asset) ~= "function" then
        error("ctx.asset should be a function")
    end

    -- Verify product() is accessible
    if not ctx.product then
        error("ctx.product should be accessible")
    end
    if type(ctx.product) ~= "function" then
        error("ctx.product should be a function")
    end
end
