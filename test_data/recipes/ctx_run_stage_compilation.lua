-- Test envy.run() in stage for simple compilation
IDENTITY = "local.ctx_run_stage_compilation@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
$source = @'
#include <stdio.h>
int main() { printf("Hello\n"); return 0; }
'@
Set-Content -Path hello.c -Value $source
Set-Content -Path compile_log.txt -Value "Compiling hello.c..."
Add-Content -Path compile_log.txt -Value "Compilation successful"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
cat > hello.c <<'EOF'
#include <stdio.h>
int main() { printf("Hello\n"); return 0; }
EOF
echo "Compiling hello.c..." > compile_log.txt
echo "Compilation successful" >> compile_log.txt
    ]])
  end
end
