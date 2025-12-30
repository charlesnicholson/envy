-- Test that manifest default_shell is respected in install string
IDENTITY = "local.install_default_shell@v1"

-- Cache-managed (no check verb)
-- Need fetch for cache-managed packages
function FETCH(tmp_dir, options)
    -- Empty fetch
end

-- Use default shell (will use system default)
INSTALL = "echo 'install shell test' > output.txt"
