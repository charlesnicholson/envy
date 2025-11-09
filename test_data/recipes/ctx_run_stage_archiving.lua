-- Test ctx.run() in stage for creating archives
identity = "local.ctx_run_stage_archiving@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create archives
  ctx.run([[
    # Create a directory structure
    mkdir -p archive_test/subdir
    echo "file1" > archive_test/file1.txt
    echo "file2" > archive_test/subdir/file2.txt

    # Create tar archive
    tar czf archive_test.tar.gz archive_test/

    # Verify archive was created
    test -f archive_test.tar.gz && echo "Archive created" > archive_log.txt
  ]])
end
