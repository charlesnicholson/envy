-- Test spec with a SETUP pair missing INSTALL (should error)
IDENTITY = "local.string_check_only@v1"
USER_MANAGED = true
SETUP = {
  main = {
    CHECK = "echo checking",
  },
}
