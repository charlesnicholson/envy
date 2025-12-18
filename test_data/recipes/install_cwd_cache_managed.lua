-- Test install cwd = install_dir for cache-managed packages
IDENTITY = "local.install_cwd_cache@v1"

-- No check verb, so this is cache-managed
-- Need a fetch source for cache-managed packages
function FETCH(ctx)
    -- Empty fetch - we're just testing install cwd
end

function INSTALL(ctx)
    -- Write a file using relative path
    if envy.PLATFORM == "windows" then
        ctx.run('"test" | Out-File -FilePath cwd_marker.txt')
    else
        ctx.run("echo 'test' > cwd_marker.txt")
    end

    -- Verify it's in install_dir by checking relative path works
    local test_cmd = envy.PLATFORM == "windows"
        and 'if (Test-Path cwd_marker.txt) { exit 0 } else { exit 1 }'
        or "test -f cwd_marker.txt"
    local res = ctx.run(test_cmd, {quiet = true})

    if res.exit_code ~= 0 then
        error("Marker file not accessible via relative path - cwd issue")
    end

    -- Also verify install_dir contains the file
    local marker_path = ctx.install_dir .. (envy.PLATFORM == "windows" and "\\cwd_marker.txt" or "/cwd_marker.txt")
    local test_cmd2 = envy.PLATFORM == "windows"
        and ('if (Test-Path \'' .. marker_path .. '\') { exit 0 } else { exit 1 }')
        or ("test -f '" .. marker_path .. "'")
    local res2 = ctx.run(test_cmd2, {quiet = true})

    if res2.exit_code ~= 0 then
        error("Marker file not in install_dir - cwd was not install_dir")
    end
end
