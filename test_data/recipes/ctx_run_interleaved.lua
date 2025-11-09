-- Test ctx.run() interleaved with other operations
identity = "local.ctx_run_interleaved@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  -- Extract
  ctx.extract_all({strip = 1})

  -- Shell work
  ctx.run([[
    echo "Step 1" > steps.txt
  ]])

  -- Rename operation (no ctx.rename, use shell)
  ctx.run([[
    if [ -f test.txt ]; then
      mv test.txt test_renamed.txt
    fi
  ]])

  -- More shell work
  ctx.run([[
    echo "Step 2" >> steps.txt
    ls > file_list.txt
  ]])

  -- Template operation (no ctx.template, use shell)
  ctx.run([[
    echo "Version: {{version}}" > version.tmpl
    version="1.0"
    sed "s/{{version}}/$version/g" version.tmpl > version.txt
  ]])

  -- Final shell verification
  ctx.run([[
    echo "Step 3" >> steps.txt
    wc -l steps.txt > step_count.txt
  ]])
end
