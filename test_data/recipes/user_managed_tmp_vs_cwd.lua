-- Test user-managed: tmp_dir for workspace, cwd is manifest directory
identity = "local.user_tmp_vs_cwd@v1"

function check(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local success, res = pcall(function()
        return ctx.run("test -f '" .. marker .. "'", {quiet = true})
    end)

    return success and res.exit_code == 0
end

function install(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end

    -- Verify tmp_dir is exposed and accessible
    assert(ctx.tmp_dir ~= nil, "tmp_dir should be exposed")

    -- Write to tmp_dir
    ctx.run("echo 'tmp test' > '" .. ctx.tmp_dir .. "/tmp_marker.txt'")

    -- Write to cwd
    ctx.run("echo 'cwd test' > cwd_marker.txt")

    -- Verify the tmp_dir file is NOT in cwd (different directories)
    -- Use pcall since test -f throws on non-zero
    local success, res2 = pcall(function()
        return ctx.run("test -f tmp_marker.txt", {quiet = true})
    end)
    if success and res2.exit_code == 0 then
        error("tmp_marker.txt found in cwd - tmp_dir appears to be the same as cwd")
    end

    -- Verify the file IS in tmp_dir
    local res3 = ctx.run("test -f '" .. ctx.tmp_dir .. "/tmp_marker.txt'", {quiet = true})
    if res3.exit_code ~= 0 then
        error("Could not find file in tmp_dir")
    end

    -- Clean up cwd marker
    ctx.run("rm -f cwd_marker.txt", {quiet = true})

    -- Create marker file to indicate success
    ctx.run("touch '" .. marker .. "'")
end
