-- Test user-managed tmp_dir lifecycle
IDENTITY = "local.user_tmp_lifecycle@v1"

function CHECK(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local success, res = pcall(function()
        return ctx.run("test -f '" .. marker .. "'", {quiet = true})
    end)

    -- If call succeeded and exit code is 0, marker exists
    return success and res.exit_code == 0
end

function INSTALL(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end

    -- Verify tmp_dir exists and is accessible
    assert(ctx.tmp_dir ~= nil, "tmp_dir should be exposed")

    -- Write a file to tmp_dir to verify it works
    ctx.run("echo 'test' > " .. ctx.tmp_dir .. "/test_file.txt")

    -- Verify the file was created
    local res = ctx.run("test -f " .. ctx.tmp_dir .. "/test_file.txt", {quiet = true})
    if res.exit_code ~= 0 then
        error("Could not write to tmp_dir")
    end

    -- Create marker file to indicate success
    ctx.run("touch '" .. marker .. "'")
end
