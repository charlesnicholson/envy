-- Test chained table field access patterns
IDENTITY = "local.check_table_chained@v1"

function CHECK(ctx)
    -- Test chained access: ctx.run(...).field
    local out = ctx.run("echo 'chained'", {capture = true}).stdout
    assert(out:match("chained"), "chained stdout access should work")

    local code = ctx.run("echo 'test'", {quiet = true}).exit_code
    assert(code == 0, "chained exit_code access should work")

    return true
end

function INSTALL(ctx)
    -- Not reached since check returns true
end
