-- User-managed package with string check form
-- Demonstrates check = "command" syntax that runs shell command
IDENTITY = "local.user_managed_string_check@v1"

-- String check: command exits 0 if "installed", non-zero otherwise
-- Platform-specific: use commands that exist on all platforms
if ENVY_PLATFORM == "windows" then
    CHECK = "powershell -Command \"Test-Path $env:TEMP\\envy-test-marker-string\""
else
    CHECK = "test -f $HOME/.envy-test-marker-string"
end

-- Empty fetch (required for validation)
function FETCH(ctx)
end

function INSTALL(ctx)
    -- Create marker file for next check
    local marker
    if ENVY_PLATFORM == "windows" then
        marker = os.getenv("TEMP") .. "\\envy-test-marker-string"
    else
        marker = os.getenv("HOME") .. "/.envy-test-marker-string"
    end

    local f = io.open(marker, "w")
    if not f then
        error("Failed to create marker file: " .. marker)
    end
    f:write("installed by user_managed_string_check")
    f:close()

    -- No mark_install_complete() - user-managed
end
