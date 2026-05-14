-- Test spec with string CHECK but no INSTALL (should error)
IDENTITY = "local.string_check_only@v1"
USER_MANAGED = true
CHECK = "echo checking"
