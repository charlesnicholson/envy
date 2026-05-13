-- Spec with syntax error
IDENTITY = "test.syntax@v1"
USER_MANAGED = true

function CHECK(project_root, options
  -- missing closing paren
  return false
end
