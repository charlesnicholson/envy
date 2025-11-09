-- Test ctx.run() with conditional operations
identity = "local.ctx_run_complex_conditional@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Complex conditionals
  ctx.run([[
    # Check OS and do different things
    if [ "$(uname)" = "Darwin" ]; then
      echo "Running on macOS" > os_info.txt
      echo "Use BSD commands" >> os_info.txt
    elif [ "$(uname)" = "Linux" ]; then
      echo "Running on Linux" > os_info.txt
      echo "Use GNU commands" >> os_info.txt
    else
      echo "Unknown OS" > os_info.txt
    fi

    # Check file exists and process
    if [ -f file1.txt ]; then
      wc -l file1.txt > test_info.txt
      echo "File processed" >> test_info.txt
    else
      echo "No file to process" > test_info.txt
    fi

    # Multi-level conditionals
    if [ -d . ]; then
      if [ -w . ]; then
        echo "Directory is writable" > dir_check.txt
      else
        echo "Directory is read-only" > dir_check.txt
      fi
    fi
  ]])
end
