-- Test ctx.run() in stage for code generation
identity = "local.ctx_run_stage_generation@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Generate files programmatically
  ctx.run([[
    # Generate a header file
    cat > generated.h <<EOF
#ifndef GENERATED_H
#define GENERATED_H
#define VERSION "1.0.0"
#endif
EOF

    # Generate a simple script
    cat > generated.sh <<'SCRIPT'
#!/usr/bin/env bash
echo "Generated script"
SCRIPT

    chmod +x generated.sh
  ]])
end
