-- Test ctx.run() mixed with ctx.template()
identity = "local.ctx_run_with_template@v1"

fetch = {
  url = "test_data/archives/test.tar.gz",
  sha256 = "ef981609163151ccb8bfd2bdae5710c525a149d29702708fb1c63a415713b11c"
}

stage = function(ctx)
  ctx.extract_all({strip = 1})

  -- Create a template with ctx.run
  ctx.run([[
    echo "Hello {{name}}, you are {{age}} years old" > greeting.tmpl
  ]])

  -- Process it with shell substitution (no ctx.template method)
  ctx.run([[
    name="Alice"
    age="30"
    sed "s/{{name}}/$name/g; s/{{age}}/$age/g" greeting.tmpl > greeting.txt
  ]])

  -- Verify with ctx.run
  ctx.run([[
    grep "Alice" greeting.txt > template_check.txt
    grep "30" greeting.txt >> template_check.txt
  ]])
end
