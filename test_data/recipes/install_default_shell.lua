-- Test that manifest default_shell is respected in install string
identity = "local.install_default_shell@v1"

-- Cache-managed (no check verb)
-- Need fetch for cache-managed packages
function fetch(ctx)
    -- Empty fetch
end

-- Use default shell (will use system default)
install = "echo 'install shell test' > output.txt"
