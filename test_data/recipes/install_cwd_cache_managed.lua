-- Test install cwd = install_dir for cache-managed packages
IDENTITY = "local.install_cwd_cache@v1"

-- No check verb, so this is cache-managed
-- Need a fetch source for cache-managed packages
function FETCH(ctx)
    -- Empty fetch - we're just testing install cwd
end

function INSTALL(ctx)
    -- Write a file using relative path
    ctx.run("echo 'test' > cwd_marker.txt")

    -- Verify it's in install_dir by checking relative path works
    local res = ctx.run("test -f cwd_marker.txt", {quiet = true})

    if res.exit_code ~= 0 then
        error("Marker file not accessible via relative path - cwd issue")
    end

    -- Also verify install_dir contains the file
    local marker_path = ctx.install_dir .. "/cwd_marker.txt"
    local res2 = ctx.run("test -f '" .. marker_path .. "'", {quiet = true})

    if res2.exit_code ~= 0 then
        error("Marker file not in install_dir - cwd was not install_dir")
    end
end
