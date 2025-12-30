-- INVALID: User-managed package that tries to use install_dir (which is nil)
-- This recipe demonstrates user-managed packages cannot access install_dir
IDENTITY = "local.user_managed_invalid@v1"

-- Has check verb (makes it user-managed)
function CHECK(project_root, options)
    return false  -- Always needs work
end

-- User-managed packages receive nil for install_dir
function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Try to use install_dir, which should be nil for user-managed packages
    if install_dir == nil then
        error("install_dir not available for user-managed package local.user_managed_invalid@v1")
    end
    -- This should not be reached since install_dir is nil
    local path = install_dir .. "/test.txt"
end
