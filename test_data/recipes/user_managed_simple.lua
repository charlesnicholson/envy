-- Simple user-managed package: check verb + install, no mark_install_complete
-- This recipe simulates a system package wrapper (like brew install python)
IDENTITY = "local.user_managed_simple@v1"

-- Check if "package" is already installed (simulated by marker file)
function CHECK(ctx)
    local marker = os.getenv("ENVY_TEST_MARKER_SIMPLE")
    if not marker then
        error("ENVY_TEST_MARKER_SIMPLE must be set")
    end

    local f = io.open(marker, "r")
    if f then
        f:close()
        return true
    end
    return false
end

-- Install the "package" (create marker file)
function INSTALL(ctx)
    local marker = os.getenv("ENVY_TEST_MARKER_SIMPLE")
    if not marker then
        error("ENVY_TEST_MARKER_SIMPLE must be set")
    end

    local f = io.open(marker, "w")
    if not f then
        error("Failed to create marker file: " .. marker)
    end
    f:write("installed by user_managed_simple")
    f:close()

    -- User-managed packages must NOT call mark_install_complete()
    -- Cache entry will be purged after this function completes
end
