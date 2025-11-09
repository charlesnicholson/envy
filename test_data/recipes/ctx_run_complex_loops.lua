-- Test ctx.run() with loops and iterations
identity = "local.ctx_run_complex_loops@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Various loop patterns
  ctx.run([[
    # Simple for loop
    for i in {1..5}; do
      echo "Iteration $i" >> loop_output.txt
    done

    # Loop over files
    for f in *.txt; do
      if [ -f "$f" ]; then
        echo "Processing $f" >> file_loop.txt
      fi
    done

    # While loop
    counter=0
    while [ $counter -lt 3 ]; do
      echo "Counter: $counter" >> while_loop.txt
      counter=$((counter + 1))
    done

    # Loop with conditionals
    for num in {1..10}; do
      if [ $((num % 2)) -eq 0 ]; then
        echo "$num is even" >> even_numbers.txt
      else
        echo "$num is odd" >> odd_numbers.txt
      fi
    done
  ]])
end
