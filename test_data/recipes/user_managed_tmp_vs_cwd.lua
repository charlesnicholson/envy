-- Test user-managed: tmp_dir for workspace, cwd is manifest directory
IDENTITY = "local.user_tmp_vs_cwd@v1"

function CHECK(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local test_cmd = ENVY_PLATFORM == "windows"
        and ('if (Test-Path \'' .. marker .. '\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function()
        return ctx.run(test_cmd, {quiet = true})
    end)

    return success and res.exit_code == 0
end

function INSTALL(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_CWD_MARKER")
    if not marker then error("ENVY_TEST_TMP_CWD_MARKER not set") end

    -- Verify tmp_dir is exposed and accessible
    assert(ctx.tmp_dir ~= nil, "tmp_dir should be exposed")

    -- Write to tmp_dir
    local path_sep = ENVY_PLATFORM == "windows" and "\\" or "/"
    local tmp_file = ctx.tmp_dir .. path_sep .. "tmp_marker.txt"
    if ENVY_PLATFORM == "windows" then
        ctx.run('"tmp test" | Out-File -FilePath \'' .. tmp_file .. '\'')
        ctx.run('"cwd test" | Out-File -FilePath cwd_marker.txt')
    else
        ctx.run("echo 'tmp test' > '" .. tmp_file .. "'")
        ctx.run("echo 'cwd test' > cwd_marker.txt")
    end

    -- Verify the tmp_dir file is NOT in cwd (different directories)
    -- Use pcall since test -f throws on non-zero
    local test_cmd1 = ENVY_PLATFORM == "windows"
        and 'if (Test-Path tmp_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f tmp_marker.txt"
    local success, res2 = pcall(function()
        return ctx.run(test_cmd1, {quiet = true})
    end)
    if success and res2.exit_code == 0 then
        error("tmp_marker.txt found in cwd - tmp_dir appears to be the same as cwd")
    end

    -- Verify the file IS in tmp_dir
    local test_cmd2 = ENVY_PLATFORM == "windows"
        and ('if (Test-Path \'' .. tmp_file .. '\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. tmp_file .. "'")
    local res3 = ctx.run(test_cmd2, {quiet = true})
    if res3.exit_code ~= 0 then
        error("Could not find file in tmp_dir")
    end

    -- Clean up cwd marker
    if ENVY_PLATFORM == "windows" then
        ctx.run('Remove-Item -Force -ErrorAction SilentlyContinue cwd_marker.txt', {quiet = true})
    else
        ctx.run("rm -f cwd_marker.txt", {quiet = true})
    end

    -- Create marker file to indicate success
    if ENVY_PLATFORM == "windows" then
        ctx.run('New-Item -ItemType File -Force -Path \'' .. marker .. '\' | Out-Null')
    else
        ctx.run("touch '" .. marker .. "'")
    end
end
