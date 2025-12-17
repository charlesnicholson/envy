-- Test user-managed tmp_dir lifecycle
IDENTITY = "local.user_tmp_lifecycle@v1"

function CHECK(ctx)
    local marker = os.getenv("ENVY_TEST_TMP_MARKER")
    if not marker then error("ENVY_TEST_TMP_MARKER not set") end

    -- Check if marker exists (non-zero means doesn't exist, need install)
    -- Use pcall since ctx.run throws on non-zero exit
    local test_cmd = ENVY_PLATFORM == "windows"
        and ('if (Test-Path \'' .. marker .. '\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker .. "'")
    local success, res = pcall(function()
        return ctx.run(test_cmd, {quiet = true})
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
    local path_sep = ENVY_PLATFORM == "windows" and "\\" or "/"
    local test_file = ctx.tmp_dir .. path_sep .. "test_file.txt"
    if ENVY_PLATFORM == "windows" then
        ctx.run('"test" | Out-File -FilePath \'' .. test_file .. '\'')
    else
        ctx.run("echo 'test' > " .. test_file)
    end

    -- Verify the file was created
    local test_cmd = ENVY_PLATFORM == "windows"
        and ('if (Test-Path \'' .. test_file .. '\') { exit 0 } else { exit 1 }')
        or ("test -f " .. test_file)
    local res = ctx.run(test_cmd, {quiet = true})
    if res.exit_code ~= 0 then
        error("Could not write to tmp_dir")
    end

    -- Create marker file to indicate success
    if ENVY_PLATFORM == "windows" then
        ctx.run('New-Item -ItemType File -Force -Path \'' .. marker .. '\' | Out-Null')
    else
        ctx.run("touch '" .. marker .. "'")
    end
end
