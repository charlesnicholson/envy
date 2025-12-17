-- INVALID: User-managed package that tries to use forbidden cache-managed APIs
-- This recipe demonstrates user-managed packages cannot access cache directories
IDENTITY = "local.user_managed_invalid@v1"

-- Has check verb (makes it user-managed)
function CHECK(ctx)
    return false  -- Always needs work
end

-- Attempts to use cache-managed APIs (should error)
function INSTALL(ctx)
    -- User-managed packages have restricted API access
    -- Attempting to call extract_all will trigger an error
    ctx.extract_all()
end
