-- INVALID: User-managed package that incorrectly calls mark_install_complete()
-- This recipe violates the check XOR cache constraint and should error
IDENTITY = "local.user_managed_invalid@v1"

-- Has check verb (makes it user-managed)
function CHECK(ctx)
    return false  -- Always needs work
end

-- But incorrectly calls mark_install_complete (cache-managed behavior)
function INSTALL(ctx)
    -- This should trigger validation error:
    -- "Recipe local.user_managed_invalid@v1 has check verb (user-managed)
    --  but called mark_install_complete()"
    ctx.mark_install_complete()
end
