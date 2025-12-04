identity = "local.ctx_asset_user_provider@v1"

function check(ctx)
  return true
end

function install(ctx)
  -- User-managed: no cache artifacts, no mark_install_complete
end
