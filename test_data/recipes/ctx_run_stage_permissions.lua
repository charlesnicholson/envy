-- Test ctx.run() in stage for setting permissions
identity = "local.ctx_run_stage_permissions@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Set file permissions
  ctx.run([[
    chmod +x file1.txt
    ls -l file1.txt > permissions.txt
    touch executable.sh
    chmod 755 executable.sh
    ls -l executable.sh >> permissions.txt
  ]])
end
