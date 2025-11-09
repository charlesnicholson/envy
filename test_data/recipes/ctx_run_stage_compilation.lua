-- Test ctx.run() in stage for simple compilation
identity = "local.ctx_run_stage_compilation@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  if ENVY_PLATFORM == "windows" then
    ctx.run([[
$source = @'#include <stdio.h>\nint main() { printf("Hello\\n"); return 0; }\n'@\nSet-Content -Path hello.c -Value $source\nSet-Content -Path compile_log.txt -Value \"Compiling hello.c...\"\nAdd-Content -Path compile_log.txt -Value \"Compilation successful\"\n    ]], { shell = "powershell" })
  else
    ctx.run([[
cat > hello.c <<'EOF'
#include <stdio.h>
int main() { printf("Hello\n"); return 0; }
EOF
echo "Compiling hello.c..." > compile_log.txt
echo "Compilation successful" >> compile_log.txt
    ]])
  end
end
