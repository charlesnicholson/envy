-- Test ctx.run() mixed with ctx.rename()
identity = "local.ctx_run_with_rename@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Use ctx.run to create a file
  ctx.run([[
    echo "original name" > original.txt
  ]])

  -- Use ctx.run to rename it (no ctx.rename method)
  ctx.run([[
    mv original.txt renamed.txt
  ]])

  -- Use ctx.run to verify rename
  ctx.run([[
    test -f renamed.txt && echo "Rename verified" > rename_check.txt
    test ! -f original.txt && echo "Original gone" >> rename_check.txt
  ]])
end
