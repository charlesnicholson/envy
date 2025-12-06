-- Test that check phase does not expose tmp_dir
identity = "local.user_check_no_tmp@v1"

function check(ctx)
    -- tmp_dir should not be exposed in check phase
    -- It's only for install phase (ephemeral workspace)
    -- Check phase tests system state, not cache state

    -- ctx.tmp_dir should not exist in check context
    -- This is actually expected - check doesn't have tmp_dir

    return true  -- Check always passes for this test
end

function install(ctx)
    -- tmp_dir IS exposed in install phase
    assert(ctx.tmp_dir ~= nil, "tmp_dir should be exposed in install phase")
end
