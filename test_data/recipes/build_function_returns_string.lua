-- Test build phase: build = function(ctx, opts) that returns a string to execute
IDENTITY = "local.build_function_returns_string@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = {strip = 1}

BUILD = function(ctx, opts)
  print("BUILD function executing, preparing to return script")

  -- Do some setup work first
  if ENVY_PLATFORM == "windows" then
    ctx.run("mkdir setup_dir 2> nul", { shell = ENVY_SHELL.CMD })
  else
    ctx.run("mkdir -p setup_dir")
  end

  -- Return a script to be executed
  if ENVY_PLATFORM == "windows" then
    return [[mkdir output_from_returned_script 2> nul & echo returned_script_artifact > output_from_returned_script\marker.txt]]
  else
    return [[
      mkdir -p output_from_returned_script
      echo "returned_script_artifact" > output_from_returned_script/marker.txt
    ]]
  end
end
