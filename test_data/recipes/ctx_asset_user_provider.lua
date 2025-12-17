IDENTITY = "local.ctx_asset_user_provider@v1"

function CHECK(ctx)
  return true
end

function INSTALL(ctx)
  -- User-managed: ephemeral workspace, no persistent cache artifacts
end
