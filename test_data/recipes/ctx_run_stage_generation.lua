-- Test envy.run() in stage for code generation
IDENTITY = "local.ctx_run_stage_generation@v1"

FETCH = {
  source = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

STAGE = function(fetch_dir, stage_dir, tmp_dir, options)
  envy.extract_all(fetch_dir, stage_dir, {strip = 1})

  if envy.PLATFORM == "windows" then
    envy.run([[
$header = @"
#ifndef GENERATED_H
#define GENERATED_H
#define VERSION \"1.0.0\"
#endif
"@
Set-Content -Path generated.h -Value $header
Set-Content -Path generated.bat -Value "@echo off`necho Generated script"
# Also produce generated.sh for test parity
Set-Content -Path generated.sh -Value "echo Generated script"
    ]], { shell = ENVY_SHELL.POWERSHELL })
  else
    envy.run([[
cat > generated.h <<EOF
#ifndef GENERATED_H
#define GENERATED_H
#define VERSION "1.0.0"
#endif
EOF

cat > generated.sh <<'SCRIPT'
#!/usr/bin/env bash
echo "Generated script"
SCRIPT
chmod +x generated.sh
    ]])
  end
end
