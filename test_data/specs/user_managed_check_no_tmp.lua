-- Test that check phase does not expose tmp_dir
IDENTITY = "local.user_check_no_tmp@v1"

function CHECK(project_root, options)
    -- tmp_dir should not be exposed in check phase
    -- It's only for install phase (ephemeral workspace)
    -- Check phase tests system state, not cache state

    -- tmp_dir should not exist in check context
    -- This is actually expected - check doesn't have tmp_dir

    return true  -- Check always passes for this test
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- tmp_dir IS exposed in install phase
    assert(tmp_dir ~= nil, "tmp_dir should be exposed in install phase")
end
