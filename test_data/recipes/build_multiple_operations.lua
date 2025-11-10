-- Test build phase: multiple operations in sequence
identity = "local.build_multiple_operations@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = {strip = 1}

build = function(ctx)
  print("Testing multiple operations")

  -- Operation 1: Create initial structure
  ctx.run([[
    mkdir -p step1
    echo "step1_output" > step1/data.txt
  ]])

  -- Operation 2: Copy to next stage
  ctx.copy("step1", "step2")

  -- Operation 3: Modify in step2
  ctx.run([[
    echo "step2_additional" >> step2/data.txt
    echo "step2_new" > step2/new.txt
  ]])

  -- Operation 4: Move to final location
  ctx.move("step2", "final")

  -- Operation 5: Verify final state
  ctx.run([[
    test -d final || exit 1
    test ! -d step2 || exit 1
    grep -q step1_output final/data.txt || exit 1
    grep -q step2_additional final/data.txt || exit 1
    test -f final/new.txt || exit 1
    echo "All operations completed"
  ]])

  print("Multiple operations successful")
end
