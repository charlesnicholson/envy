-- Test ctx.run() with nested operations and complex scripts
identity = "local.ctx_run_complex_nested@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create a complex nested structure
  ctx.run([[
    # Create nested directories and files
    mkdir -p level1/{level2a,level2b}/{level3a,level3b}

    # Populate with files
    for l2 in level1/level2*; do
      for l3 in $l2/level3*; do
        echo "Content in $l3" > "$l3/data.txt"
      done
    done

    # Process the tree
    find level1 -type f -name "data.txt" | while read file; do
      dirname=$(dirname "$file")
      basename=$(basename "$dirname")
      echo "Found in $basename" >> found_files.txt
    done

    # Create a summary
    echo "Total directories: $(find level1 -type d | wc -l)" > summary.txt
    echo "Total files: $(find level1 -type f | wc -l)" >> summary.txt

    # Nested conditionals and loops
    for dir in level1/level2*; do
      if [ -d "$dir" ]; then
        count=$(find "$dir" -type f | wc -l)
        if [ $count -gt 0 ]; then
          echo "Directory $dir has $count files" >> dir_summary.txt
        fi
      fi
    done
  ]])
end
