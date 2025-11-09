-- Test ctx.run() in stage for cleanup operations
identity = "local.ctx_run_stage_cleanup@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Cleanup unwanted files
  ctx.run([[
    # Remove any backup files
    find . -name "*.bak" -delete

    # Remove temporary files
    rm -f *.tmp

    # Clean up empty directories
    find . -type d -empty -delete

    echo "Cleanup complete" > cleanup_log.txt
  ]])
end
