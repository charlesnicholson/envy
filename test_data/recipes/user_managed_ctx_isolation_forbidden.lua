-- Test that user-managed packages cannot access forbidden ctx APIs
IDENTITY = "local.user_managed_ctx_isolation_forbidden@v1"

function CHECK(ctx)
    return false  -- Always needs install
end

function INSTALL(ctx)
    -- Attempt to access a forbidden API (will be set by test harness via env var)
    local forbidden_api = os.getenv("ENVY_TEST_FORBIDDEN_API")
    if not forbidden_api then
        error("ENVY_TEST_FORBIDDEN_API must be set")
    end

    -- Try to access the forbidden API
    local value = ctx[forbidden_api]

    -- Forbidden APIs should throw errors when accessed/called
    local success, err = pcall(function()
        -- If it's a function, try to call it
        if type(value) == "function" then
            value()
        elseif type(value) == "string" then
            -- Just accessing it succeeded, which is bad
            error("Should have thrown error")
        end
    end)

    if success then
        error("Expected error when accessing ctx." .. forbidden_api .. " but succeeded")
    end

    -- Verify error message mentions it's not available for user-managed
    if not err:find("not available for user%-managed") then
        error("Expected error about user-managed, got: " .. err)
    end
end
