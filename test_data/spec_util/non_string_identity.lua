-- Spec with IDENTITY that is not a string
IDENTITY = 12345
USER_MANAGED = true

function CHECK(project_root, options)
  return false
end
