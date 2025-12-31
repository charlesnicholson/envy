-- Test that user-managed packages CAN access envy.* APIs
IDENTITY = "local.user_managed_ctx_isolation_allowed@v1"

function CHECK(project_root, options)
    return false  -- Always needs install
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify identity is accessible
    if not IDENTITY then
        error("IDENTITY should be accessible")
    end
    if IDENTITY ~= "local.user_managed_ctx_isolation_allowed@v1" then
        error("IDENTITY mismatch: " .. IDENTITY)
    end

    -- Verify envy.run is accessible
    if not envy.run then
        error("envy.run should be accessible")
    end
    if type(envy.run) ~= "function" then
        error("envy.run should be a function")
    end

    -- Actually call envy.run to verify it works
    local result = envy.run("echo test", {capture = true, quiet = true})
    if result.exit_code ~= 0 then
        error("envy.run failed: " .. result.exit_code)
    end
    if not result.stdout:find("test") then
        error("envy.run stdout missing 'test': " .. result.stdout)
    end

    -- Verify envy.package is accessible (even if we don't have dependencies)
    if not envy.package then
        error("envy.package should be accessible")
    end
    if type(envy.package) ~= "function" then
        error("envy.package should be a function")
    end

    -- Verify envy.product is accessible
    if not envy.product then
        error("envy.product should be accessible")
    end
    if type(envy.product) ~= "function" then
        error("envy.product should be a function")
    end
end
