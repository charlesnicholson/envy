-- Test ctx.run() creating many files
identity = "local.ctx_run_edge_many_files@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create many files
  ctx.run([[
    mkdir -p many_files
    for i in {1..50}; do
      echo "File $i content" > "many_files/file_$i.txt"
    done
    echo "Created many files" > many_files_marker.txt
  ]])
end
