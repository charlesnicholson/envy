-- Test that user-managed packages can access tmp_dir
IDENTITY = "local.user_managed_ctx_isolation_tmp_dir@v1"

function CHECK(project_root, options)
    return false  -- Always needs install
end

function INSTALL(install_dir, stage_dir, fetch_dir, tmp_dir, options)
    -- Verify tmp_dir is accessible
    if not tmp_dir then
        error("tmp_dir should be accessible for user-managed packages")
    end

    -- Verify it's a valid path string
    if type(tmp_dir) ~= "string" then
        error("tmp_dir should be a string, got " .. type(tmp_dir))
    end

    -- Create a file in tmp_dir to verify it's writable
    local test_file = tmp_dir .. "/test.txt"
    local f = io.open(test_file, "w")
    if not f then
        error("Failed to create file in tmp_dir: " .. test_file)
    end
    f:write("test content")
    f:close()

    -- Verify the file was created
    f = io.open(test_file, "r")
    if not f then
        error("Failed to read file from tmp_dir: " .. test_file)
    end
    local content = f:read("*all")
    f:close()

    if content ~= "test content" then
        error("tmp_dir file content mismatch")
    end
end
