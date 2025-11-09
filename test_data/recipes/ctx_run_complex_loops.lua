-- Test ctx.run() with loops and iterations
identity = "local.ctx_run_complex_loops@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
      Remove-Item loop_output.txt, file_loop.txt, while_loop.txt, even_numbers.txt, odd_numbers.txt -ErrorAction SilentlyContinue
      for ($i = 1; $i -le 5; $i++) {
        $line = "Iteration $i"
        if ($i -eq 1) {
          Set-Content -Path loop_output.txt -Value $line
        } else {
          Add-Content -Path loop_output.txt -Value $line
        }
      }

      foreach ($f in Get-ChildItem -Filter *.txt) {
        $line = "Processing $($f.Name)"
        if (Test-Path file_loop.txt) {
          Add-Content -Path file_loop.txt -Value $line
        } else {
          Set-Content -Path file_loop.txt -Value $line
        }
      }

      $counter = 0
      while ($counter -lt 3) {
        $line = "Counter: $counter"
        if ($counter -eq 0) {
          Set-Content -Path while_loop.txt -Value $line
        } else {
          Add-Content -Path while_loop.txt -Value $line
        }
        $counter++
      }

      for ($num = 1; $num -le 10; $num++) {
        if ($num % 2 -eq 0) {
          if (Test-Path even_numbers.txt) {
            Add-Content -Path even_numbers.txt -Value "$num is even"
          } else {
            Set-Content -Path even_numbers.txt -Value "$num is even"
          }
        } else {
          if (Test-Path odd_numbers.txt) {
            Add-Content -Path odd_numbers.txt -Value "$num is odd"
          } else {
            Set-Content -Path odd_numbers.txt -Value "$num is odd"
          }
        }
      }
    ]], { shell = "powershell" })
  else
    ctx.run([[
      for i in {1..5}; do
        echo "Iteration $i" >> loop_output.txt
      done

      for f in *.txt; do
        if [ -f "$f" ]; then
          echo "Processing $f" >> file_loop.txt
        fi
      done

      counter=0
      while [ $counter -lt 3 ]; do
        echo "Counter: $counter" >> while_loop.txt
        counter=$((counter + 1))
      done

      for num in {1..10}; do
        if [ $((num % 2)) -eq 0 ]; then
          echo "$num is even" >> even_numbers.txt
        else
          echo "$num is odd" >> odd_numbers.txt
        fi
      done
    ]])
  end
end
