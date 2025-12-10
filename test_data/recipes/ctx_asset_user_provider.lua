IDENTITY = "local.ctx_asset_user_provider@v1"

function CHECK(ctx)
  return true
end

function INSTALL(ctx)
  -- User-managed: no cache artifacts, no mark_install_complete
end
